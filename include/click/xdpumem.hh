#pragma once

#include <click/xdp.hh>

extern "C" {
#include <bpf/xsk.h>
}

struct xsk_umem_info {
    struct xsk_ring_prod fq;
    struct xsk_ring_cons cq;
    struct xsk_umem* umem;
    void* buffer;
};

class XDPUMEM {
   public:
    XDPUMEM(u32 num_frames, u32 frame_size, u32 fill_size, u32 comp_size, void *buf);

    u64 next();

   private:
    size_t _head{0};
    u32 _num_frames, _frame_size, _fill_size, _comp_size;
    xsk_umem_info* _umem{nullptr};

    friend class XDPSock;
};

