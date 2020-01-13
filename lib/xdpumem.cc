/*
 * xdpumem.{cc,hh} A UMEM allocator
 *
 */

#include <click/xdpumem.hh>

using std::runtime_error;

XDPUMEM::XDPUMEM(u32 num_frames, u32 frame_size, u32 fill_size, u32 comp_size)
    : _num_frames{num_frames},
      _frame_size{frame_size}, 
      _fill_size{fill_size}, 
      _comp_size{comp_size} 
{
    // reserve memory for UMEM
    void* buffer = mmap(
        NULL, 
        num_frames * frame_size, 
        PROT_READ | PROT_WRITE, 
        MAP_PRIVATE | MAP_ANONYMOUS, 
        -1, 
        0
    );

    if (buffer == MAP_FAILED) {
        printf("ERROR: mmap failed\n");
        exit(EXIT_FAILURE);
    }

    struct xsk_umem_config cfg = {
        .fill_size = fill_size,
        .comp_size = comp_size,
        .frame_size = frame_size,
        .frame_headroom = XSK_UMEM__DEFAULT_FRAME_HEADROOM,
        .flags = 0,
    };
    int ret;

    _umem = static_cast<xsk_umem_info*>(calloc(1, sizeof(*_umem)));
    if (!_umem) {
        throw runtime_error{"unable to create umem"};
    }

    ret = xsk_umem__create(
        &_umem->umem, 
        buffer, 
        num_frames * frame_size, 
        &_umem->fq, 
        &_umem->cq, &cfg
    );
    if (ret) {
        throw runtime_error{"umem create failed"};
    }

    _umem->buffer = buffer;
}

u64 XDPUMEM::next() {
    u64 v = _head;
    _head = _head++ & (_num_frames - 1);
    return v;
}

