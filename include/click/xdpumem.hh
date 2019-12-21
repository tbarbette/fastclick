#pragma once

#include <click/xdp.hh>

extern "C" {
#include <bpf/xsk.h>
}

struct xsk_umem_info {
	struct xsk_ring_prod fq;
	struct xsk_ring_cons cq;
	struct xsk_umem *umem;
	void *buffer;
};

class XDPUMEM {

  public:
    XDPUMEM(size_t size);

    u64 alloc();
    void free(u64);


  private:
    static constexpr u8 RADIX{64},
                        SHIFT{6};   // log2(RADIX)

    u8                _height;
    size_t            _size,
                      _nodes;
    u64              *_trie;
    xsk_umem_info    *_umem_info{nullptr};

    bool _alloc(u64 m, u64 & addr);
    bool _free(u64 m , u64 & addr);

};

