#ifndef CLICK_DPDKDEVICE_HH
#define CLICK_DPDKDEVICE_HH

/**
 * Prevent bug under some configurations
 * (like travis-ci's one) where these
 * macros get undefined.
 */
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

#if HAVE_FLOW_API
    #include <click/flowruleparser.hh>
#endif

#if RTE_VERSION >= RTE_VERSION_NUM(17,11,0,0) && RTE_VERSION < RTE_VERSION_NUM(19,2,0,0)
    #include <rte_bus_pci.h>
#endif

#include <click/packet.hh>
#include <click/error.hh>
#include <click/hashtable.hh>
#include <click/vector.hh>
#include <click/args.hh>
#include <click/etheraddress.hh>
#include <click/timer.hh>

#if RTE_VERSION < RTE_VERSION_NUM(19,8,0,0)
#define rte_ipv4_hdr ipv4_hdr
#define rte_ether_addr ether_addr
#endif

#if RTE_VERSION < RTE_VERSION_NUM(19,8,0,0)
#define rte_ipv4_hdr ipv4_hdr
#define rte_ether_addr ether_addr
#endif

/**
 * Unified type for DPDK port IDs.
 * Until DPDK v17.05 was uint8_t
 * After DPDK v17.05 has been uint16_t
 */
#ifndef PORTID_T_DEFINED
    #define PORTID_T_DEFINED
    typedef uint8_t portid_t;
#else
    // Already defined in <testpmd.h>
#endif

CLICK_DECLS

enum FlowControlMode {FC_UNSET, FC_NONE, FC_RX, FC_TX, FC_FULL};

class DPDKDeviceArg;

#if HAVE_INT64_TYPES
typedef uint64_t counter_t;
#else
typedef uint32_t counter_t;
#endif

extern bool dpdk_enabled;

class DPDKDevice {
public:

    portid_t port_id;

    DPDKDevice() CLICK_COLD;
    DPDKDevice(portid_t port_id) CLICK_COLD;

    struct DevInfo {
        inline DevInfo() :
            vendor_id(PCI_ANY_ID), vendor_name(), device_id(PCI_ANY_ID), driver(0),
            init_mac(), init_mtu(0), init_rss(-1), init_fc_mode(FC_UNSET),
            rx_queues(0, false), tx_queues(0, false), n_rx_descs(0), n_tx_descs(0),
            num_pools(0), promisc(false),
	    mq_mode((enum rte_eth_rx_mq_mode)-1), mq_mode_str(""),
            rx_offload(0), tx_offload(0),	
	    flow_isolate(false),
            vlan_filter(false), vlan_strip(false), vlan_extend(false), vf_vlan(),
            lro(false), jumbo(false)
        {
            rx_queues.reserve(128);
            tx_queues.reserve(128);
        }

        void print_device_info() {
            if (device_id == PCI_ANY_ID) {
                return;
            }
            click_chatter("                Vendor   ID: %d", vendor_id);
            click_chatter("                Vendor Name: %s", vendor_name.c_str());
            click_chatter("                Device   ID: %d", device_id);
            click_chatter("                Driver Name: %s", driver);
            click_chatter("                MAC Address: %s", init_mac.unparse().c_str());
            click_chatter("      Maximum Transfer Unit: %u", init_mtu);
            click_chatter("Receive Side Scaling queues: %d", init_rss);
            click_chatter("          Flow Control Mode: %d", init_fc_mode);
            click_chatter("             # of Rx Queues: %d", rx_queues.size());
            click_chatter("             # of Tx Queues: %d", tx_queues.size());
            click_chatter("             # of Rx  Descs: %d", n_rx_descs);
            click_chatter("             # of Tx  Descs: %d", n_tx_descs);
            click_chatter("          # of memory pools: %d", num_pools);
            click_chatter("             Reception Mode: %s", mq_mode_str.c_str());
            click_chatter("           Promiscuous Mode: %s", promisc? "true":"false");
            click_chatter("         Flow API Isolation: %s", flow_isolate? "true":"false");
            click_chatter("           Rx Offloads flag: %" PRIu64, rx_offload);
            click_chatter("           Tx Offloads flag: %" PRIu64, tx_offload);
            click_chatter("          VLAN    Filtering: %s", vlan_filter? "true":"false");
            click_chatter("          VLAN    Stripping: %s", vlan_strip? "true":"false");
            click_chatter("          VLAN QinQ(extend): %s", vlan_extend? "true":"false");
            click_chatter("      Virtual Function VLAN: %d", vf_vlan.size());
            click_chatter("Large Receive Offload (LRO): %s", lro ? "true":"false");
            click_chatter("    Rx Jumbo Frames Offload: %s", jumbo ? "true":"false");
        }

        uint16_t vendor_id;
        String vendor_name;
        uint16_t device_id;
        const char *driver;
        EtherAddress init_mac;
        uint16_t init_mtu;
        int init_rss;
        FlowControlMode init_fc_mode;
        Vector<bool> rx_queues;
        Vector<bool> tx_queues;
        unsigned n_rx_descs;
        unsigned n_tx_descs;
        int num_pools;
        bool promisc;
        enum rte_eth_rx_mq_mode mq_mode;
        String mq_mode_str;
        uint64_t rx_offload;
        uint64_t tx_offload;
        bool flow_isolate;
        bool vlan_filter;
        bool vlan_strip;
        bool vlan_extend;
        Vector<int> vf_vlan;
        bool lro;
        bool jumbo;
    };

#if HAVE_FLOW_API
    void initialize_flow_rule_manager(
        const portid_t &port_id,
        ErrorHandler   *errh
    );
#endif

    portid_t get_port_id() { return port_id; }

    int add_rx_queue(
        unsigned &queue_id, bool promisc, bool vlan_filter, bool vlan_strip, bool vlan_extend,
        bool lro, bool jumbo, unsigned n_desc, ErrorHandler *errh
    ) CLICK_COLD;

    int add_tx_queue(
        unsigned &queue_id, unsigned n_desc,
        ErrorHandler *errh
    ) CLICK_COLD;

    EtherAddress get_mac();
    void set_init_mac(EtherAddress mac);
    void set_init_mtu(uint16_t mtu);
    void set_init_rss_max(int rss_max);
    void set_init_fc_mode(FlowControlMode fc);
    void set_rx_offload(uint64_t offload);
    void set_tx_offload(uint64_t offload);
    void set_init_flow_isolate(const bool &flow_isolate);

    inline void set_isolation_mode(const bool &isolated) {
        info.flow_isolate = isolated;
        if (info.flow_isolate) {
            rte_flow_isolate(port_id, 1, 0);
        } else {
            rte_flow_isolate(port_id, 0, 0);
        }
    };
    inline bool isolated() { return info.flow_isolate; };



    unsigned int get_nb_rxdesc();
    unsigned int get_nb_txdesc();

    uint16_t get_device_vendor_id();
    String get_device_vendor_name();
    uint16_t get_device_id();
    const char *get_device_driver();
    int set_rss_max(int max);

    static unsigned int dev_count() {
#if RTE_VERSION >= RTE_VERSION_NUM(18,05,0,0)
        return rte_eth_dev_count_avail();
#else
        return rte_eth_dev_count();
#endif
    }

    static struct rte_mempool *get_mpool(unsigned int);

    static int get_port_numa_node(portid_t port_id);

#if HAVE_FLOW_API
    int set_mode(
        String mode, int num_pools, Vector<int> vf_vlan,
        const String &flow_rules_filename, ErrorHandler *errh
    );
#else
    int set_mode(
        String mode, int num_pools, Vector<int> vf_vlan,
        ErrorHandler *errh
    );
#endif
    rte_eth_rx_mq_mode get_mode();
    String get_mode_str();

    static int initialize(ErrorHandler *errh);

    static int static_initialize(ErrorHandler *errh);

    static int static_cleanup();

#if CLICK_PACKET_USE_DPDK
    inline static bool is_dpdk_packet(Packet* p) {
        return true;
    }

    inline static bool is_dpdk_buffer(Packet* p) {
        return true;
    }
#else
    inline static bool is_dpdk_packet(Packet* p) {
        return p->buffer_destructor() == DPDKDevice::free_pkt;
    }

    inline static bool is_dpdk_buffer(Packet* p) {
        return is_dpdk_packet(p) || (p->data_packet() && is_dpdk_packet(p->data_packet()));
    }
#endif

    inline static rte_mbuf* get_pkt(unsigned numa_node);
    inline static rte_mbuf* get_pkt();
    inline static struct rte_mbuf* get_mbuf(Packet* p, bool create, int node, bool reset = true);

    static void free_pkt(unsigned char *, size_t, void *pktmbuf);

    static unsigned int get_nb_txdesc(const portid_t &port_id);

#if HAVE_FLOW_API
    static int configure_nic(const portid_t &port_id);
#endif

    static void cleanup(ErrorHandler *errh);

    static Vector<int> NB_MBUF;
    static int DEFAULT_NB_MBUF;
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

    static unsigned DEF_DEV_RXDESC;
    static unsigned DEF_DEV_TXDESC;

    static unsigned DEF_RING_NDESC;
    static unsigned DEF_BURST_SIZE;

    static unsigned RING_FLAGS;
    static unsigned RING_SIZE;
    static unsigned RING_POOL_CACHE_SIZE;
    static unsigned RING_PRIV_DATA_SIZE;

    static struct rte_mempool** _pktmbuf_pools;

    inline struct DevInfo get_info() { return info; };
    inline int nb_rx_queues();
    inline int nb_tx_queues();
    inline int nb_vf_pools();

    struct rte_ether_addr gen_mac(int a, int b);

    /*
    * TXInternalQueue is a ring of DPDK buffers pointers (rte_mbuf *) awaiting
    * to be sent. It is used as an internal buffer to be passed to DPDK ring
    * queue.
    * |-> index is the index of the first valid packet awaiting to be sent, while
    *     nr_pending is the number of packets.
    * |-> index + nr_pending may be greater than
    *     _internal_tx_queue_size but index should be wrapped-around.
    */
    class TXInternalQueue {
        public:
            TXInternalQueue() : pkts(0), index(0), nr_pending(0) { }

            // Array of DPDK Buffers
            struct rte_mbuf **pkts;
            // Index of the first valid packet in the packets array
            unsigned int index;
            // Number of valid packets awaiting to be sent after index
            unsigned int nr_pending;

            // Timer to limit time a batch will take to be completed
            Timer timeout;
    } __attribute__((aligned(64)));

    static bool initialized() {
        return _is_initialized;
    }

private:

    enum Dir { RX, TX };

    struct DevInfo info;


    static int get_nb_mbuf(int socket);
    static bool _is_initialized;
    static HashTable<portid_t, DPDKDevice> _devs;
    static unsigned _nr_pktmbuf_pools;
    static bool no_more_buffer_msg_printed;

    int initialize_device(ErrorHandler *errh) CLICK_COLD;
    int add_queue(Dir dir, unsigned &queue_id,
                    bool promisc, bool vlan_filter, bool vlan_strip, bool vlan_extend,
                    bool lro, bool jumbo, unsigned n_desc, ErrorHandler *errh) CLICK_COLD;

    static int alloc_pktmbufs(ErrorHandler* errh) CLICK_COLD;

    static DPDKDevice *ensure_device(const portid_t &port_id) {
        return &(_devs.find_insert(port_id, DPDKDevice(port_id)).value());
    }

    static DPDKDevice *get_device(const portid_t &port_id) {
        return &(_devs.find(port_id).value());
    }

#if RTE_VERSION < RTE_VERSION_NUM(18,05,0,0)
    static int get_port_from_pci(uint32_t domain, uint8_t bus, uint8_t dev_id, uint8_t function) {
       struct rte_eth_dev_info dev_info;

       uint16_t count = rte_eth_dev_count();
       for (portid_t port_id = 0 ; port_id < count; ++port_id) {
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
#endif

    friend class DPDKDeviceArg;
    friend class DPDKInfo;
};

class DPDKRing { public:

    DPDKRing() CLICK_COLD;
    ~DPDKRing() CLICK_COLD;

    int parse(Args* args);

    struct rte_mempool *_message_pool;
    String _MEM_POOL;
    String _PROC_1;
    String _PROC_2;

    unsigned _ndesc;
    unsigned _burst_size;
    short _numa_zone;
    int _flags;

    struct rte_ring *_ring;
    counter_t _count;

    bool _force_create;
    bool _force_lookup;

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

/** @class FlowControlModeArg
  @brief Parser class for flow control mode. */
class FlowControlModeArg { public:
    static bool parse(const String &str, FlowControlMode &result, const ArgContext &args = ArgContext());
    static String unparse(FlowControlMode mode);
};

template<> struct DefaultArg<FlowControlMode> : public FlowControlModeArg {};

/**
 * Get a DPDK mbuf from a packet. If the packet buffer is a DPDK buffer, it will
 *     give that one. If it isn't, it will allocate a new mbuf from a DPDK pool
 *     and copy its content.
 *     If compiled with CLICK_PACKET_USE_DPDK, it will simply return the packet
 *     casted as it's already a DPDK buffer.
 */
inline struct rte_mbuf* DPDKDevice::get_mbuf(Packet* p, bool create, int node, bool reset) {
    struct rte_mbuf* mbuf;
    #if CLICK_PACKET_USE_DPDK
    mbuf = p->mb();
    #else
    if (likely(DPDKDevice::is_dpdk_packet(p) && (mbuf = (struct rte_mbuf *)((unsigned char*) p->buffer() - sizeof(rte_mbuf)) ))
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
            if (reset)
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

int DPDKDevice::nb_rx_queues() {
    return info.rx_queues.size();
};

int DPDKDevice::nb_tx_queues() {
    return info.tx_queues.size();
};

int DPDKDevice::nb_vf_pools() {
    return info.num_pools;
};

CLICK_ENDDECLS

#endif
