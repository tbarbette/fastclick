#ifndef CLICK_DPDKDEVICE_HH
#define CLICK_DPDKDEVICE_HH

//Prevent bug under some configurations (like travis-ci's one) where these macros get undefined
#ifndef UINT8_MAX
#define UINT8_MAX 255
#endif
#ifndef UINT16_MAX
#define UINT16_MAX 65535
#endif

#include <rte_common.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_pci.h>
#include <rte_version.h>

#include <click/packet.hh>
#include <click/error.hh>
#include <click/hashtable.hh>
#include <click/vector.hh>
#include <click/args.hh>
#include <click/etheraddress.hh>

CLICK_DECLS
class DPDKDeviceArg;

#if HAVE_INT64_TYPES
typedef uint64_t counter_t;
#else
typedef uint32_t counter_t;
#endif

extern bool dpdk_enabled;

class DPDKDevice {
public:

    unsigned port_id;

    DPDKDevice() : port_id(-1), info() {
    }

    DPDKDevice(unsigned port_id) : port_id(port_id) {
    } CLICK_COLD;

    int add_rx_queue(int &queue_id, bool promisc,
                             unsigned n_desc, ErrorHandler *errh) CLICK_COLD;

    int add_tx_queue(int &queue_id, unsigned n_desc,
                             ErrorHandler *errh) CLICK_COLD;

    void set_mac(EtherAddress mac);

    unsigned int get_nb_txdesc();

    static struct rte_mempool *get_mpool(unsigned int);

    static int get_port_numa_node(unsigned port_id);

    static int initialize(ErrorHandler *errh);

    static int static_cleanup();

    inline static bool is_dpdk_packet(Packet* p) {
        return p->buffer_destructor() == DPDKDevice::free_pkt;
    }

    inline static bool is_dpdk_buffer(Packet* p) {
        return is_dpdk_packet(p) || (p->data_packet() && is_dpdk_packet(p->data_packet()));
    }

    inline static rte_mbuf* get_pkt(unsigned numa_node);
    inline static rte_mbuf* get_pkt();
    inline static struct rte_mbuf* get_mbuf(Packet* p, bool create, int node);

    static void free_pkt(unsigned char *, size_t, void *pktmbuf);

    static unsigned int get_nb_txdesc(unsigned port_id);

    static int NB_MBUF;
    static int MBUF_DATA_SIZE;
    static int MBUF_SIZE;
    static int MBUF_CACHE_SIZE;
    static int RX_PTHRESH;
    static int RX_HTHRESH;
    static int RX_WTHRESH;
    static int TX_PTHRESH;
    static int TX_HTHRESH;
    static int TX_WTHRESH;
    static String MEMPOOL_PREFIX;

    static unsigned DEF_RING_NDESC;
    static unsigned DEF_BURST_SIZE;

    static unsigned RING_FLAGS;
    static unsigned RING_SIZE;
    static unsigned RING_POOL_CACHE_SIZE;
    static unsigned RING_PRIV_DATA_SIZE;

    static struct rte_mempool** _pktmbuf_pools;

private:

    enum Dir { RX, TX };

    struct DevInfo {
        inline DevInfo() :
            rx_queues(0,false), tx_queues(0,false), promisc(false), n_rx_descs(0),
            n_tx_descs(0), mac() {
            rx_queues.reserve(128);
            tx_queues.reserve(128);
        }

        Vector<bool> rx_queues;
        Vector<bool> tx_queues;
        bool promisc;
        unsigned n_rx_descs;
        unsigned n_tx_descs;
        EtherAddress mac;
    };

    struct DevInfo info;

    static bool _is_initialized;
    static HashTable<unsigned, DPDKDevice> _devs;
    static int _nr_pktmbuf_pools;
    static bool no_more_buffer_msg_printed;

    int initialize_device(ErrorHandler *errh) CLICK_COLD;
    int add_queue(Dir dir, int &queue_id, bool promisc,
                   unsigned n_desc, ErrorHandler *errh) CLICK_COLD;

    static bool alloc_pktmbufs() CLICK_COLD;

    static DPDKDevice* get_device(unsigned port_id) {
       return &(_devs.find_insert(port_id, DPDKDevice(port_id)).value());
    }

    static int get_port_from_pci(uint16_t domain, uint8_t bus, uint8_t dev_id, uint8_t function) {
       struct rte_eth_dev_info dev_info;
       for (uint8_t port_id = 0 ; port_id < rte_eth_dev_count(); ++port_id) {
          rte_eth_dev_info_get(port_id, &dev_info);
          struct rte_pci_addr addr = dev_info.pci_dev->addr;
          if (addr.domain   == domain &&
              addr.bus      == bus &&
              addr.devid    == dev_id &&
              addr.function == function)
              return port_id;
       }
       return -1;
    }

    friend class DPDKDeviceArg;
};

/** @class DPDKDeviceArg
  @brief Parser class for DPDK Port, either an integer or a PCI address. */
class DPDKDeviceArg { public:
    static bool parse(const String &str, DPDKDevice* &result, const ArgContext &args = ArgContext());
    static String unparse(DPDKDevice* dev) {
        return String(dev->port_id);
    }
};

template<> struct DefaultArg<DPDKDevice*> : public DPDKDeviceArg {};

/**
 * Get a DPDK mbuf from a packet. If the packet buffer is a DPDK buffer, it will
 *     give that one. If it isn't, it will allocate a new mbuf from a DPDK pool
 *     and copy its content.
 *     If compiled with CLICK_PACKET_USE_DPDK, it will simply return the packet
 *     casted as it's already a DPDK buffer.
 */
inline struct rte_mbuf* DPDKDevice::get_mbuf(Packet* p, bool create, int node) {
    struct rte_mbuf* mbuf;
    #if CLICK_PACKET_USE_DPDK
    mbuf = p->mb();
    #else
    if (likely(DPDKDevice::is_dpdk_packet(p) && (mbuf = (struct rte_mbuf *) p->destructor_argument()))
        || unlikely(p->data_packet() && DPDKDevice::is_dpdk_packet(p->data_packet()) && (mbuf = (struct rte_mbuf *) p->data_packet()->destructor_argument()))) {
        /* If the packet is an unshared DPDK packet, we can send
         *  the mbuf as it to DPDK*/
        rte_pktmbuf_pkt_len(mbuf) = p->length();
        rte_pktmbuf_data_len(mbuf) = p->length();
        mbuf->data_off = p->headroom();
        if (p->shared()) {
            /*Prevent DPDK from freeing the buffer. When all shared packet
             * are freed, DPDKDevice::free_pkt will effectively destroy it.*/
            rte_mbuf_refcnt_update(mbuf, 1);
        } else {
            //Reset buffer, let DPDK free the buffer when it wants
            p->reset_buffer();
        }
    } else {
        if (create) {
            /*The packet is not a DPDK packet, or it is shared : we need to allocate a mbuf and
             * copy the packet content to it.*/
            mbuf = DPDKDevice::get_pkt(node);
            if (mbuf == 0) {
                return NULL;
            }
            memcpy((void*)rte_pktmbuf_mtod(mbuf, unsigned char *),p->data(),p->length());
            rte_pktmbuf_pkt_len(mbuf) = p->length();
            rte_pktmbuf_data_len(mbuf) = p->length();
        } else
            return NULL;
    }
    #endif
    return mbuf;
}

inline rte_mbuf* DPDKDevice::get_pkt(unsigned numa_node) {
    struct rte_mbuf* mbuf = rte_pktmbuf_alloc(get_mpool(numa_node));
    if (unlikely(!mbuf)) {
        if (!DPDKDevice::no_more_buffer_msg_printed)
            click_chatter("No more DPDK buffer available ! Try using "
                               "DPDKInfo to allocate more.");
        else
            DPDKDevice::no_more_buffer_msg_printed = true;
    }
    return mbuf;
}

inline rte_mbuf* DPDKDevice::get_pkt() {
    return get_pkt(rte_socket_id());
}

CLICK_ENDDECLS

#endif
