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
    XDPUMEM(size_t size);

    u64 next();

   private:
    size_t _size, _head{0};
    xsk_umem_info* _umem_info{nullptr};
};

