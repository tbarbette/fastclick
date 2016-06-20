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
#include <click/hashmap.hh>
#include <click/vector.hh>

CLICK_DECLS

class DPDKDevice {
public:

    static struct rte_mempool *get_mpool(unsigned int);
    inline static struct rte_mbuf* get_mbuf(Packet* p, bool create = true, int node = 0);

    static int get_port_numa_node(unsigned port_id);

    static int add_rx_device(unsigned port_id, int &queue_id, bool promisc,
                             unsigned n_desc, ErrorHandler *errh);

    static int add_tx_device(unsigned port_id, int &queue_id, unsigned n_desc,
                             ErrorHandler *errh);
    static int initialize(ErrorHandler *errh);
	static int static_cleanup();

    inline static bool is_dpdk_packet(Packet* p) {
        return p->buffer_destructor() == DPDKDevice::free_pkt;
    }

    inline static bool is_dpdk_buffer(Packet* p) {
        return is_dpdk_packet(p) || (p->data_packet() && is_dpdk_packet(p->data_packet()));
    }

    inline static bool is_valid_dpdk_packet(Packet* p) {
        struct rte_mbuf* mb = (struct rte_mbuf*)p->destructor_argument();
        return mb && (mb->buf_addr == p->buffer());
        /*for (int i = 0; i < _nr_pktmbuf_pools; i++) {
            if (p->buffer() > _pktmbuf_pools[i]->elt_va_start && p->buffer() < _pktmbuf_pools[i]->elt_va_start)
                return true;
        }
        return false;*/
    }

    inline static rte_mbuf* get_pkt(unsigned numa_node) {
        return rte_pktmbuf_alloc(get_mpool(numa_node));
    }

    inline static rte_mbuf* get_pkt() {
        return get_pkt(rte_socket_id());
    }

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

private:

    enum Dir { RX, TX };

    struct DevInfo {
        inline DevInfo() :
            rx_queues(0,false), tx_queues(0,false), promisc(false), n_rx_descs(0),
            n_tx_descs(0) {
            rx_queues.reserve(128);
            tx_queues.reserve(128);
        }

        Vector<bool> rx_queues;
        Vector<bool> tx_queues;
        bool promisc;
        unsigned n_rx_descs;
        unsigned n_tx_descs;
    };

    static bool _is_initialized;
    static HashMap<unsigned, DevInfo> _devs;
    static struct rte_mempool** _pktmbuf_pools;
    static int _nr_pktmbuf_pools;

    static int initialize_device(unsigned port_id, DevInfo &info,
                                 ErrorHandler *errh) CLICK_COLD;

    static void add_pool(const struct rte_mempool *, void *) CLICK_COLD;
    static bool alloc_pktmbufs() CLICK_COLD;

    static int add_device(unsigned port_id, Dir dir, int &queue_id,
                          bool promisc, unsigned n_desc, ErrorHandler *errh)
        CLICK_COLD;
};

/**
 * Get a DPDK mbuf from a packet. If the packet buffer is a DPDK buffer, it will
 * 	give that one. If it isn't, it will allocate a new mbuf from a DPDK pool
 * 	and copy its content.
 * 	If compiled with CLICK_PACKET_USE_DPDK, it will simply return the packet
 * 	casted as it's already a DPDK buffer.
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
                click_chatter("Out of DPDK buffer ! Check your configuration for "
                        "packet leaks or increase the number of buffer with DPDKInfo().");
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


CLICK_ENDDECLS

#endif
