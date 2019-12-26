#include <click/xdpumem.hh>

/*
 * xdpumem.{cc,hh} A UMEM allocator
 *
 */

XDPUMEM::XDPUMEM(size_t size) : _size{size} {}

u64 XDPUMEM::next() {
    u64 v = _head;
    _head = _head++ % _size;
    return v;
}

