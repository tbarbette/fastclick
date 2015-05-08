#ifndef CLICK_DPDKCONFIG_HH
#define CLICK_DPDKCONFIG_HH

#include <click/packet.hh>
#include <click/error.hh>
#include <click/hashmap.hh>

CLICK_DECLS

// Those should really be user-configurable
#define NB_MBUF 65536*8
#define MBUF_SIZE (2048 + sizeof (struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define MBUF_CACHE_SIZE 256
#define RX_PTHRESH 8
#define RX_HTHRESH 8
#define RX_WTHRESH 4
#define TX_PTHRESH 36
#define TX_HTHRESH 0
#define TX_WTHRESH 0

class DpdkDevice {
public:
    // Move to private
    enum Dir { RX, TX };

    static struct rte_mempool * get_mpool(unsigned int);

    static int get_port_numa_node(unsigned port_no);

    static int add_device(unsigned port_no, Dir dir, unsigned queue_no,
                          bool promisc, ErrorHandler *errh);

    static int add_rx_device(unsigned port_no, unsigned queue_no,
                             bool promisc, ErrorHandler *errh);
    static int add_tx_device(unsigned port_no, unsigned queue_no,
                             ErrorHandler *errh);
    static int initialize(ErrorHandler *errh);

#if !CLICK_DPDK_POOLS
    inline static bool is_dpdk_packet(Packet* p) {
            return p->buffer_destructor() == DpdkDevice::free_pkt;
    }
    static void free_pkt(unsigned char *, size_t, void *pktmbuf);

    static void fake_free_pkt(unsigned char *, size_t, void *pktmbuf);
#endif

    static unsigned int get_nb_txdesc(unsigned port_no);

    static void set_rx_descs(unsigned port_no, unsigned rx_descs,
            ErrorHandler *errh);

    static void set_tx_descs(unsigned port_no, unsigned tx_descs,
            ErrorHandler *errh);
};

CLICK_ENDDECLS

#endif // CLICK_DPDKCONFIG_HH
