#pragma once

#include <click/xdp.hh>
#include <click/config.h>
#include <click/packet.hh>
#include <click/xdp.hh>
#include <click/xdpumem.hh>

#include <vector>
#include <string>
#include <memory>

extern "C" {
#include <linux/if_xdp.h>
#include <bpf/libbpf.h>
#include <bpf/xsk.h>
}

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
	u32 outstanding_fq;
};

class XDPSock {

  public:
    XDPSock(XDPInterfaceSP xfx, XDPUMEMSP xm, u32 queue_id, bool trace=false);

    void rx(PBuf &pb);
    void tx(Packet *p);
    void kick();

    inline u32 queue_id() const { return _queue_id; }
    pollfd poll_fd() const;

  private:
    //void configure_umem();
    void configure_socket();
    void kick_tx();

    void tx_complete();
    void fq_replenish();
    void ingress(PBuf &pb);

    std::shared_ptr<XDPInterface>   _xfx{nullptr};
    u32                             _queue_id,
                                    _prog_id;
    xsk_socket_info                 *_xsk;
    //xsk_umem_info                   *_umem;
    void                            *_umem_buf;
    bool                            _trace;
    pollfd                          _fd;

    std::shared_ptr<XDPUMEM> _umem_mgr{nullptr};

    static int _poll_timeout;

};

static inline void free_pkt(unsigned char *pkt, size_t, void *pktmbuf)
{
    // notthing to do, packets are part of an emulation wide ringbuffer
}

