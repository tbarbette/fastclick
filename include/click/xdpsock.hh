#include <click/xdp.hh>
#include <click/config.h>
#include <click/packet.hh>
#include <click/xdp.hh>

#include <vector>
#include <string>
#include <memory>

extern "C" {
#include <linux/if_xdp.h>
#include <bpf/libbpf.h>
#include <bpf/xsk.h>
}

#define FRAME_SIZE    XSK_UMEM__DEFAULT_FRAME_SIZE
#define NUM_RX_DESCS  XSK_RING_CONS__DEFAULT_NUM_DESCS
#define NUM_TX_DESCS  XSK_RING_PROD__DEFAULT_NUM_DESCS
#define NUM_DESCS     (NUM_RX_DESCS + NUM_TX_DESCS)
#define NUM_FRAMES    NUM_DESCS
#define BATCH_SIZE    64
#define FRAME_HEADROOM XSK_UMEM__DEFAULT_FRAME_HEADROOM
#define FRAME_TAILROOM FRAME_HEADROOM

struct xsk_umem_info {
	struct xsk_ring_prod fq;
	struct xsk_ring_cons cq;
	struct xsk_umem *umem;
	void *buffer;
};

struct xsk_socket_info {
	struct xsk_ring_cons rx;
	struct xsk_ring_prod tx;
	struct xsk_umem_info *umem;
	struct xsk_socket *xsk;
	unsigned long rx_npkts;
	unsigned long tx_npkts;
	unsigned long prev_rx_npkts;
	unsigned long prev_tx_npkts;
	u32 outstanding_tx;
};

class XDPSock {

  public:
    XDPSock(XDPInterfaceSP xfx, u32 queue_id);

    std::vector<Packet*>  rx();
    void                  tx(Packet *p);
    void                  kick();

    inline u32 queue_id() const { return _queue_id; }
    pollfd poll_fd() const;

  private:
    void configure_umem();
    void configure_socket();
    void kick_tx();

    std::shared_ptr<XDPInterface>   _xfx;
    u32                             _queue_id,
                                    _prog_id;
    xsk_socket_info                 *_xsk;
    xsk_umem_info                   *_umem;
    void                            *_umem_buf;

};

static inline void free_pkt(unsigned char *pkt, size_t, void *pktmbuf)
{
   // right now we copy packets into click. an optimiazation that could be nice
   // is that instead of allocating a chunk of memory for a packet we 'allocate'
   // a frame from the destination xdp device. this is not really allocation in
   // the sense that all the frames for the device are allocated up front, it's
   // more of a taking of ownership. This would eliminate the need for any
   // dynamic memory allocation. However we would still need to copy the packet
   // from the source UMEM to the target UMEM.
   free(pkt);
}

