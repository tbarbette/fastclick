#include <click/xdp.hh>

extern "C" {
#include <linux/if_xdp.h>
#include <bpf/libbpf.h>
#include <bpf/xsk.h>
}

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

};
