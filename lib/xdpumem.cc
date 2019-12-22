#include <click/xdpumem.hh>

/*
 * xdpumem.{cc,hh} A UMEM allocator
 *
 * The XDPUMEM class is a generic packet frame allocator for memory mapped
 * packet frame buffers. It's designed to preside over large memory maps
 * (hundreds of gigabytes as a typical case).
 *
 * XDPUMEM imposes a tree structure over a memory region. Consider the problem
 * of managing 1TB of memory for packet frames that are 2048 bytes. The total
 * number of frames that we need to address is 
 *
 *    1024*1e9 / 2048 = 500e6
 *
 * Now consider a tree structure where each node in the tree is a 64-bit bitmap.
 * The total number of leaves needed to cover the space is
 *
 *    500e6 / 64 = 7812500
 *
 * This leads to a tree height of
 *
 *    ceil(log(7812500, 64)) + 1 = ceil(3.8 ) +1
 *                               = 5
 *
 * The total space required for the tree is then
 *
 *         64^0 + 64^1 + 64^2 + 64^3 + 64^4 = 17043521 nodes
 *    (17043521 nodes) * (8 bytes per node) = 136348168 bytes
 *                                          ~ 133 MB
 *
 * which is ~ 0.01% of the total memory mapped.
 *
 * The primary benefit of this structure is that only 5 bitmap operations are
 * required for allocating/deallocating memory from the UMEM pool.
 *
 */

XDPUMEM::XDPUMEM(size_t size) : _size{size} {
    // calculate height of trie
    _height = ceil(log(size) + log(RADIX)) + 1;

    // calculate number of nodes in trie
    _nodes = 0;
    for (size_t i = 0; i < _height; i++) {
        _nodes += pow(RADIX, i);
    }

    // allocate/zero trie
    _trie = (u64*)calloc(_nodes, sizeof(u64));
}

static inline bool full(u64 node) { return node == ~0UL; }

// get the lowest position zero bit
static inline ulong ffz(u64 node)
{
  asm("rep; bsf %1,%0"
		: "=r" (node)
		: "r" (~node));
	return node;
}

static inline void set_bit(long nr, u64 *addr)
{
  // the way kernel does it, possible optimization later
	//asm volatile(__ASM_SIZE(bts) " %1,%0" : : ADDR, "Ir" (nr) : "memory");
  *addr |= 1UL << nr;
}

static inline void clear_bit(long nr, u64 *addr)
{
  // the way kernel does it, possible optimization later
	//asm volatile(__ASM_SIZE(btr) " %1,%0" : : ADDR, "Ir" (nr) : "memory");
  *addr &= ~(1UL << nr);
}

static inline u64 ulog2(u64 x)
{
  u64 l{0};
  while (x >>= 1) { ++l; }
  return l;
}

// returns index of frame, -1 (~0UL) if full
u64 XDPUMEM::alloc() {

  if (unlikely(full(_trie[0]))) {
    return -1;
  }

  u64 addr{0};
  _alloc(_nodes, addr);

  return addr;

}

bool XDPUMEM::_alloc(u64 m, u64 & addr) {

  // get the bitmap value of the current node
  u64 *value = &_trie[addr];

  // find first zero in the current node's bitmap
  u64 next_pos = ffz(*value);

  // how far in total address space a position at this depth swings us
  m >>= SHIFT;

  // calculate addition to address based on position and shift (implicitly 
  // based on depth because `m` is shifting at each level of recursion)
  addr += m*next_pos;

  // if we are at the bottom of the tree set the bit and recurse back up
  if (m == 1) {
    set_bit(next_pos, value);
    return full(*value);
  }

  // if we are not at the bottom of the tree, continue to recurse down
  bool isfull = _alloc(m, addr);

  // once recursing back up the stack, if the node below us reports as full,
  // mark that in the bitmap at this depth of the tree
  if (unlikely(isfull)) {
    set_bit(next_pos, value);
  }
 
  return full(*value);

}

void XDPUMEM::free(u64 x) {


}
