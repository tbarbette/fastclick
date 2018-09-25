// -*- related-file-name: "../include/click/packet.hh" -*-
/*
 * packet.{cc,hh} -- a packet structure. In the Linux kernel, a synonym for
 * `struct sk_buff'
 * Eddie Kohler, Robert Morris, Nickolai Zeldovich
 *
 * Copyright (c) 1999-2001 Massachusetts Institute of Technology
 * Copyright (c) 2008-2011 Regents of the University of California
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#define CLICK_PACKET_DEPRECATED_ENUM
#include <click/packet.hh>
#include <click/packet_anno.hh>
#include <click/glue.hh>
#include <click/sync.hh>
#include <click/ring.hh>
#include <click/vector.hh>
#include <click/netmapdevice.hh>
#if CLICK_USERLEVEL || CLICK_MINIOS
# include <unistd.h>
#endif
#if HAVE_DPDK
# include <rte_malloc.h>
# include <click/dpdkdevice.hh>
#endif
#if CLICK_PACKET_USE_DPDK
# include <rte_lcore.h>
# include <rte_mempool.h>
#endif
CLICK_DECLS

/** @file packet.hh
 * @brief The Packet class models packets in Click.
 */

/** @class Packet
 * @brief A network packet.
 * @nosubgrouping
 *
 * Click's Packet class represents network packets within a router.  Packet
 * objects are passed from Element to Element via the Element::push() and
 * Element::pull() functions.  The vast majority of elements handle packets.
 *
 * A packet consists of a <em>data buffer</em>, which stores the actual packet
 * wire data, and a set of <em>annotations</em>, which store extra information
 * calculated about the packet, such as the destination address to be used for
 * routing.  Every Packet object has different annotations, but a data buffer
 * may be shared among multiple Packet objects, saving memory and speeding up
 * packet copies.  (See Packet::clone.)  As a result a Packet's data buffer is
 * not writable.  To write into a packet, turn it into a nonshared
 * WritablePacket first, using uniqueify(), push(), or put().
 *
 * <h3>Data Buffer</h3>
 *
 * A packet's data buffer is a single flat array of bytes.  The buffer may be
 * larger than the actual packet data, leaving unused spaces called
 * <em>headroom</em> and <em>tailroom</em> before and after the data proper.
 * Prepending headers or appending data to a packet can be quite efficient if
 * there is enough headroom or tailroom.
 *
 * The relationships among a Packet object's data buffer variables is shown
 * here:
 *
 * <pre>
 *                     data()               end_data()
 *                        |                      |
 *       |<- headroom() ->|<----- length() ----->|<- tailroom() ->|
 *       |                v                      v                |
 *       +================+======================+================+
 *       |XXXXXXXXXXXXXXXX|   PACKET CONTENTS    |XXXXXXXXXXXXXXXX|
 *       +================+======================+================+
 *       ^                                                        ^
 *       |<------------------ buffer_length() ------------------->|
 *       |                                                        |
 *    buffer()                                               end_buffer()
 * </pre>
 *
 * Most code that manipulates packets is interested only in data() and
 * length().
 *
 * To create a Packet, call one of the make() functions.  To destroy a Packet,
 * call kill().  To clone a Packet, which creates a new Packet object that
 * shares this packet's data, call clone().  To uniqueify a Packet, which
 * unshares the packet data if necessary, call uniqueify().  To allocate extra
 * space for headers or trailers, call push() and put().  To remove headers or
 * trailers, call pull() and take().
 *
 * <pre>
 *                data()                          end_data()
 *                   |                                |
 *           push()  |  pull()                take()  |  put()
 *          <======= | =======>              <======= | =======>
 *                   v                                v
 *       +===========+================================+===========+
 *       |XXXXXXXXXXX|        PACKET CONTENTS         |XXXXXXXXXXX|
 *       +===========+================================+===========+
 * </pre>
 *
 * Packet objects are implemented in different ways in different drivers.  The
 * userlevel driver has its own C++ implementation.  In the linuxmodule
 * driver, however, Packet is an overlay on Linux's native sk_buff
 * object: the Packet methods access underlying sk_buff data directly, with no
 * overhead.  (For example, Packet::data() returns the sk_buff's data field.)
 *
 * <h3>Annotations</h3>
 *
 * Annotations are extra information about a packet above and beyond the
 * packet data.  Packet supports several specific annotations, plus a <em>user
 * annotation area</em> available for arbitrary use by elements.
 *
 * <ul>
 * <li><b>Header pointers:</b> Each packet has three header pointers, designed
 * to point to the packet's MAC header, network header, and transport header,
 * respectively.  Convenience functions like ip_header() access these pointers
 * cast to common header types.  The header pointers are kept up to date when
 * operations like push() or uniqueify() change the packet's data buffer.
 * Header pointers can be null, and they can even point to memory outside the
 * current packet data bounds.  For example, a MAC header pointer will remain
 * set even after pull() is used to shift the packet data past the MAC header.
 * As a result, functions like mac_header_offset() can return negative
 * numbers.</li>
 * <li><b>Timestamp:</b> A timestamp associated with the packet.  Most packet
 * sources timestamp packets when they enter the router; other elements
 * examine or modify the timestamp.</li>
 * <li><b>Device:</b> A pointer to the device on which the packet arrived.
 * Only meaningful in the linuxmodule driver, but provided in every
 * driver.</li>
 * <li><b>Packet type:</b> A small integer indicating whether the packet is
 * meant for this host, broadcast, multicast, or some other purpose.  Several
 * elements manipulate this annotation; in linuxmodule, setting the annotation
 * is required for the host network stack to process incoming packets
 * correctly.</li>
 * <li><b>Performance counter</b> (linuxmodule only): A 64-bit integer
 * intended to hold a performance counter value.  Used by SetCycleCount and
 * others.</li>
 * <li><b>Next and previous packet:</b> Pointers provided to allow elements to
 * chain packets into a doubly linked list.</li>
 * <li><b>Annotations:</b> Each packet has @link Packet::anno_size anno_size
 * @endlink bytes available for annotations.  Elements agree to use portions
 * of the annotation area to communicate per-packet information.  Macros in
 * the <click/packet_anno.hh> header file define the annotations used by
 * Click's current elements.  One common annotation is the network address
 * annotation -- see Packet::dst_ip_anno().  Routing elements, such as
 * RadixIPLookup, set the address annotation to indicate the desired next hop;
 * ARPQuerier uses this annotation to query the next hop's MAC.</li>
 * </ul>
 *
 * New packets start wth all annotations set to zero or null.  Cloning a
 * packet copies its annotations.
 */

/** @class WritablePacket
 * @brief A network packet believed not to be shared.
 *
 * The WritablePacket type represents Packet objects whose data buffers are
 * not shared.  As a result, WritablePacket's versions of functions that
 * access the packet data buffer, such as data(), end_buffer(), and
 * ip_header(), return mutable pointers (<tt>char *</tt> rather than <tt>const
 * char *</tt>).
 *
 * WritablePacket objects are created by Packet::make(), Packet::uniqueify(),
 * Packet::push(), and Packet::put(), which ensure that the returned packet
 * does not share its data buffer.
 *
 * WritablePacket's interface is the same as Packet's except for these type
 * differences.  For documentation, see Packet.
 *
 * @warning The WritablePacket convention reduces the likelihood of error
 * when modifying packet data, but does not eliminate it.  For instance, by
 * calling WritablePacket::clone(), it is possible to create a WritablePacket
 * whose data is shared:
 * @code
 * Packet *p = ...;
 * if (WritablePacket *q = p->uniqueify()) {
 *     Packet *p2 = q->clone();
 *     assert(p2);
 *     q->ip_header()->ip_v = 6;   // modifies p2's data as well
 * }
 * @endcode
 * Avoid writing buggy code like this!  Use WritablePacket selectively, and
 * try to avoid calling WritablePacket::clone() when possible. */

Packet::~Packet()
{
    // This is a convenient place to put static assertions.
    static_assert(addr_anno_offset % 8 == 0 && user_anno_offset % 8 == 0,
		  "Annotations must begin at multiples of 8 bytes.");
    static_assert(addr_anno_offset + addr_anno_size <= anno_size,
		  "Annotation area too small for address annotations.");
    static_assert(user_anno_offset + user_anno_size <= anno_size,
		  "Annotation area too small for user annotations.");
    static_assert(dst_ip_anno_offset == DST_IP_ANNO_OFFSET
		  && dst_ip6_anno_offset == DST_IP6_ANNO_OFFSET
		  && dst_ip_anno_size == DST_IP_ANNO_SIZE
		  && dst_ip6_anno_size == DST_IP6_ANNO_SIZE
		  && dst_ip_anno_size == 4
		  && dst_ip6_anno_size == 16
		  && dst_ip_anno_offset + 4 <= anno_size
		  && dst_ip6_anno_offset + 16 <= anno_size,
		  "Address annotations at unexpected locations.");
    static_assert((default_headroom & 3) == 0,
		  "Default headroom should be a multiple of 4 bytes.");
#if CLICK_LINUXMODULE
    static_assert(sizeof(Anno) <= sizeof(((struct sk_buff *)0)->cb),
		  "Anno structure too big for Linux packet annotation area.");
#endif

#if CLICK_LINUXMODULE
    panic("Packet destructor");
#elif CLICK_PACKET_USE_DPDK
    rte_panic("Packet destructor");
#else
    if (_data_packet)
	_data_packet->kill();
# if CLICK_USERLEVEL || CLICK_MINIOS
    else if (_head && _destructor) {
        if (_destructor != empty_destructor)
            _destructor(_head, _end - _head, _destructor_argument);
    } else
#  if HAVE_NETMAP_PACKET_POOL
    if (_head && NetmapBufQ::is_valid_netmap_packet(this)) {
        NetmapBufQ::local_pool()->insert_p(_head);
    } else
#  endif
    if (_head) {
            delete[] _head;
    }
# elif CLICK_BSDMODULE
    if (_m)
	m_freem(_m);
# endif
    _head = _data = 0;
#endif
}

#if !CLICK_LINUXMODULE && !CLICK_PACKET_USE_DPDK

# if HAVE_CLICK_PACKET_POOL
// ** Packet pools **

// Click configurations usually allocate & free tons of packets and it's
// important to do so quickly. This specialized packet allocator saves
// pre-initialized Packet objects, either with or without data, for fast
// reuse. It can support multithreaded deployments: each thread has its own
// pool, with a global pool to even out imbalance.

#if HAVE_DPDK_PACKET_POOL
#  define CLICK_PACKET_POOL_BUFSIZ		DPDKDevice::MBUF_DATA_SIZE
#else
#  define CLICK_PACKET_POOL_BUFSIZ		2048
#endif
#  define CLICK_PACKET_POOL_SIZE		4096 // see LIMIT in packetpool-01.testie
#  define CLICK_GLOBAL_PACKET_POOL_COUNT	32

#  if HAVE_MULTITHREAD
static __thread PacketPool *thread_packet_pool;

typedef MPMCRing<WritablePacket*,CLICK_GLOBAL_PACKET_POOL_COUNT> BatchRing;

struct GlobalPacketPool {
    BatchRing pbatch;     // batches of free packets, linked by p->prev()
                                //   p->anno_u32(0) is # packets in batch
    BatchRing pdbatch;        // batches of packet with data buffers

    PacketPool* thread_pools;   // all thread packet pools

    volatile uint32_t lock;
};
static GlobalPacketPool global_packet_pool;
#else
static PacketPool global_packet_pool = {0,0,0,0};
#  endif

/** @brief Return the local packet pool for this thread.
    @pre make_local_packet_pool() has succeeded on this thread. */
static inline PacketPool& local_packet_pool() {
#  if HAVE_MULTITHREAD
    return *thread_packet_pool;
#  else
    // If not multithreaded, there is only one packet pool.
    return global_packet_pool;
#  endif
}

/** @brief Create and return a local packet pool for this thread. */
PacketPool* WritablePacket::make_local_packet_pool() {
#  if HAVE_MULTITHREAD
    PacketPool *pp = thread_packet_pool;
    if (unlikely(!pp && (pp = new PacketPool))) {
	memset(pp, 0, sizeof(PacketPool));
	while (atomic_uint32_t::swap(global_packet_pool.lock, 1) == 1)
	    /* do nothing */;
	pp->thread_pool_next = global_packet_pool.thread_pools;
	global_packet_pool.thread_pools = pp;
	thread_packet_pool = pp;
	click_compiler_fence();
	global_packet_pool.lock = 0;
    }
    return pp;
#  else
    return &global_packet_pool;
#  endif
}

/**
 * Allocate a batch of packets without buffer
 * The returned list is a simple linked list, not a standard PacketBatch
 */
WritablePacket *
WritablePacket::pool_batch_allocate(uint16_t count)
{
        PacketPool& packet_pool = *make_local_packet_pool();

        WritablePacket *p = 0;
        WritablePacket *head = 0;
        int taken_from_pool = 0;

        while (count > 0) {
            p = packet_pool.p;
            if (!p) {
                packet_pool.pcount -= taken_from_pool;
                taken_from_pool = 0;
                p = pool_allocate();
            } else {
                packet_pool.p = static_cast<WritablePacket*>(p->next());
                taken_from_pool++;
            }
            if (head == 0) {
                head = p;
            }
            count --;
        }
        packet_pool.pcount -= taken_from_pool;

        p->set_next(0);

        return head;
}

inline WritablePacket *
WritablePacket::pool_allocate()
{
    PacketPool& packet_pool = *make_local_packet_pool();

#  if HAVE_MULTITHREAD
    if (!packet_pool.p) {
        WritablePacket *pp = global_packet_pool.pbatch.extract();
        if (pp) {
            packet_pool.p = pp;
            packet_pool.pcount = pp->anno_u32(0);
        }

    }
#  endif /* HAVE_MULTITHREAD */

        WritablePacket *p = packet_pool.p;
        if (p) {
        packet_pool.p = static_cast<WritablePacket*>(p->next());
        --packet_pool.pcount;
        } else {
        p = new WritablePacket;
        }
        return p;

}

/**
 * Allocate a packet with a buffer
 */
WritablePacket *
WritablePacket::pool_data_allocate()
{
    PacketPool& packet_pool = *make_local_packet_pool();

#  if HAVE_MULTITHREAD
    if (unlikely(!packet_pool.pd)) {
        WritablePacket *pd = global_packet_pool.pdbatch.extract();
        if (pd) {
            packet_pool.pd = pd;
            packet_pool.pdcount = pd->anno_u32(0);
        }
	}
#  endif /* HAVE_MULTITHREAD */

    WritablePacket *pd = packet_pool.pd;
    if (pd) {
        packet_pool.pd = static_cast<WritablePacket*>(pd->next());
        --packet_pool.pdcount;
    } else {
        pd = pool_allocate();
        pd->alloc_data(0,CLICK_PACKET_POOL_BUFSIZ,0);
    }
    return pd;

}

/**
 * Allocate a packet with a buffer of the specified size
 */
inline  WritablePacket *
WritablePacket::pool_allocate(uint32_t headroom, uint32_t length,
			      uint32_t tailroom)
{
    uint32_t n = headroom + length + tailroom;

    WritablePacket *p;
    if (likely(n <= CLICK_PACKET_POOL_BUFSIZ)) {
        p = pool_data_allocate();
        p->_data = p->_head + headroom;
        p->_tail = p->_data + length;
        p->_end = p->_head + CLICK_PACKET_POOL_BUFSIZ;
#if HAVE_DPDK_PACKET_POOL
       buffer_destructor_type type = p->_destructor;
#endif
        p->initialize();
#if HAVE_DPDK_PACKET_POOL
        p->_destructor = type;
#endif
    } else {
        p = pool_allocate();
        p->alloc_data(headroom,length,tailroom);
        p->initialize();
    }

	return p;
}

inline void
WritablePacket::check_packet_pool_size(PacketPool &packet_pool) {
#  if HAVE_MULTITHREAD
    if (unlikely(packet_pool.p && packet_pool.pcount >= CLICK_PACKET_POOL_SIZE)) {
        packet_pool.p->set_anno_u32(0, packet_pool.pcount);
        if (!global_packet_pool.pbatch.insert(packet_pool.p)) { //Si le nombre de batch est au max -> delete
            while (WritablePacket *p = packet_pool.p) { //On supprime le batch
                packet_pool.p = static_cast<WritablePacket *>(p->next());
                ::operator delete((void *) p);
            }
        }
        packet_pool.p = 0;
        packet_pool.pcount = 0;
    }
#  else /* !HAVE_MULTITHREAD */
    if (packet_pool.pcount == CLICK_PACKET_POOL_SIZE) {
        WritablePacket* tmp = (WritablePacket*)packet_pool.p->next();
        ::operator delete((void *) packet_pool.p);
        packet_pool.p = tmp;
        packet_pool.pcount--;
    }
#  endif /* HAVE_MULTITHREAD */
}

inline void
WritablePacket::check_data_pool_size(PacketPool &packet_pool) {
#  if HAVE_MULTITHREAD
    if (unlikely(packet_pool.pd && packet_pool.pdcount >= CLICK_PACKET_POOL_SIZE)) {
        packet_pool.pd->set_anno_u32(0, packet_pool.pdcount);
        if (!global_packet_pool.pdbatch.insert(packet_pool.pd)) {
            while (WritablePacket *pd = packet_pool.pd) {
                packet_pool.pd = static_cast<WritablePacket *>(pd->next());
#if HAVE_DPDK_PACKET_POOL
                rte_pktmbuf_free((struct rte_mbuf*)pd->destructor_argument());
#else
# if HAVE_NETMAP_PACKET_POOL
                if (NetmapBufQ::is_valid_netmap_packet(pd))
                    NetmapBufQ::local_pool()->insert_p(pd->buffer());
                else
# endif
                {
                    ::operator delete[]((unsigned char *) pd->buffer());
                }
#endif
                ::operator delete((void *) pd);
            }
        }
        packet_pool.pd = 0;
        packet_pool.pdcount = 0;
    }

#  else /* !HAVE_MULTITHREAD */
    if (packet_pool.pdcount == CLICK_PACKET_POOL_SIZE) {
        WritablePacket* tmp = (WritablePacket*)packet_pool.pd->next();
        ::operator delete((void *) packet_pool.pd);
        packet_pool.pd = tmp;
        packet_pool.pdcount--;
    }
#  endif /* HAVE_MULTITHREAD */
}

inline bool WritablePacket::is_from_data_pool(WritablePacket *p) {
#if HAVE_DPDK_PACKET_POOL
	return likely(!p->_data_packet && p->_head
			&& (p->_destructor == DPDKDevice::free_pkt));
#else
    if (likely(!p->_data_packet && p->_head && !p->_destructor)) {
# if HAVE_NETMAP_PACKET_POOL
        return NetmapBufQ::is_valid_netmap_packet(p);
# else
        if (likely(p->_end - p->_head == CLICK_PACKET_POOL_BUFSIZ)) //Is standard buffer size?
            return true;
        else
            return false;
# endif
    }
    else
        return false;
#endif

}

/**
 * @Precond _use_count == 1
 */
void
WritablePacket::recycle(WritablePacket *p)
{
    PacketPool& packet_pool = *make_local_packet_pool();
    bool data = is_from_data_pool(p);

    if (likely(data)) {
        check_data_pool_size(packet_pool);
        ++packet_pool.pdcount;
        p->set_next(packet_pool.pd);
        packet_pool.pd = p;
#if !HAVE_BATCH_RECYCLE
        assert(packet_pool.pdcount <= CLICK_PACKET_POOL_SIZE);
#endif
    } else {
        p->~WritablePacket();
        check_packet_pool_size(packet_pool);
        ++packet_pool.pcount;
        p->set_next(packet_pool.p);
        packet_pool.p = p;
#if !HAVE_BATCH_RECYCLE
        assert(packet_pool.pcount <= CLICK_PACKET_POOL_SIZE);
#endif
    }

}

/**
 * @Precond : _use_count == 1 for all packets
 */
void
WritablePacket::recycle_packet_batch(WritablePacket *head, Packet* tail, unsigned count)
{
    PacketPool& packet_pool = *make_local_packet_pool();

    Packet* next = ((head != 0)? head->next() : 0 );
    Packet* p = head;
    for (;p != 0;p=next,next=(p==0?0:p->next())) {
        ((WritablePacket*)p)->~WritablePacket();
    }
    check_packet_pool_size(packet_pool);
    packet_pool.pcount += count;
    tail->set_next(packet_pool.p);
    packet_pool.p = head;
}

/**
 * @Precond : _use_count == 1 for all packets
 */
void
WritablePacket::recycle_data_batch(WritablePacket *head, Packet* tail, unsigned count)
{
    PacketPool& packet_pool = *make_local_packet_pool();
    check_data_pool_size(packet_pool);
    packet_pool.pdcount += count;
    tail->set_next(packet_pool.pd);
    packet_pool.pd = head;
}

# endif /* HAVE_CLICK_PACKET_POOL */

inline bool
Packet::alloc_data(uint32_t headroom, uint32_t length, uint32_t tailroom)
{
    uint32_t n = length + headroom + tailroom;
    if (n < min_buffer_length) {
	tailroom = min_buffer_length - length - headroom;
	n = min_buffer_length;
    }
# if CLICK_USERLEVEL || CLICK_MINIOS
    unsigned char *d = 0;
    if (n <= CLICK_PACKET_POOL_SIZE) {
#  if HAVE_DPDK_PACKET_POOL
        struct rte_mbuf *mb = DPDKDevice::get_pkt();
        if (likely(mb)) {
          d = (unsigned char*)mb->buf_addr;
          _destructor = DPDKDevice::free_pkt;
          _destructor_argument = mb;
        } else {
            return 0;
        }
#  elif HAVE_NETMAP_PACKET_POOL
    d = NetmapBufQ::local_pool()->extract_p();
#  endif
    } else {
# if HAVE_DPDK_PACKET_POOL
        click_chatter("Warning : buffer of size %d bigger than DPDK buffer size", n);
# endif
    }
    if (!d) {
# if HAVE_DPDK
      if (dpdk_enabled)
          d = (unsigned char*)rte_malloc(0, n, 64);
      else
# endif
      d = new unsigned char[n];
    }
    if (!d)
	return false;
    _head = d;
    _data = d + headroom;
    _tail = _data + length;
    _end = _head + n;
# elif CLICK_BSDMODULE
    //click_chatter("allocate new mbuf, length=%d", n);
    if (n > MJUM16BYTES) {
	click_chatter("trying to allocate %d bytes: too many\n", n);
	return false;
    }
    struct mbuf *m;
    MGETHDR(m, M_DONTWAIT, MT_DATA);
    if (!m)
	return false;
    if (n > MHLEN) {
	if (n > MCLBYTES)
	    m_cljget(m, M_DONTWAIT, (n <= MJUMPAGESIZE ? MJUMPAGESIZE :
				     n <= MJUM9BYTES   ? MJUM9BYTES   :
 							 MJUM16BYTES));
	else
	    MCLGET(m, M_DONTWAIT);
	if (!(m->m_flags & M_EXT)) {
	    m_freem(m);
	    return false;
	}
    }
    _m = m;
    _m->m_data += headroom;
    _m->m_len = length;
    _m->m_pkthdr.len = length;
    assimilate_mbuf();
# endif /* CLICK_USERLEVEL || CLICK_BSDMODULE */
    return true;
}

#endif /* !CLICK_LINUXMODULE && !CLICK_PACKET_USE_DPDK */

/**
 * Give a hint that some packets from one thread will switch to another thread
 */
void WritablePacket::pool_transfer(int from, int to) {
    (void)from;
    (void)to;
}




/** @brief Create and return a new packet.
 * @param headroom headroom in new packet
 * @param data data to be copied into the new packet
 * @param length length of packet
 * @param tailroom tailroom in new packet (ignored when DPDK is used)
 * @return new packet, or null if no packet could be created
 *
 * The @a data is copied into the new packet.  If @a data is null, the
 * packet's data is left uninitialized.  The resulting packet's
 * buffer_length() will be at least @link Packet::min_buffer_length
 * min_buffer_length @endlink; if @a headroom + @a length + @a tailroom would
 * be less, then @a tailroom is increased to make the total @link
 * Packet::min_buffer_length min_buffer_length @endlink.
 *
 * The new packet's annotations are cleared and its header pointers are
 * null. */
WritablePacket *
Packet::make(uint32_t headroom, const void *data,
	     uint32_t length, uint32_t tailroom)
{

	#if CLICK_LINUXMODULE
		int want = 1;
		if (struct sk_buff *skb = skbmgr_allocate_skbs(headroom, length + tailroom, &want)) {
		assert(want == 1);
		// packet comes back from skbmgr with headroom reserved
		__skb_put(skb, length);	// leave space for data
		if (data)
			memcpy(skb->data, data, length);
		# if PACKET_CLEAN
			skb->pkt_type = HOST | PACKET_CLEAN;
		# else
			skb->pkt_type = HOST;
		# endif
		WritablePacket *q = reinterpret_cast<WritablePacket *>(skb);
		q->clear_annotations();
		return q;
		} else
		return 0;
#elif CLICK_PACKET_USE_DPDK
    struct rte_mbuf *mb = DPDKDevice::get_pkt();
    if (!mb) {
        click_chatter("could not alloc pktmbuf");
        return 0;
    }
    //rte_pktmbuf_prepend(mb, rte_pktmbuf_headroom(mb)); : Already done
    rte_pktmbuf_data_len(mb) = length;
    rte_pktmbuf_pkt_len(mb) = length;
    if (data)
        memcpy(rte_pktmbuf_mtod(mb, void *), data, length);
    (void) tailroom;
    return reinterpret_cast<WritablePacket *>(mb);
#else

		# if HAVE_CLICK_PACKET_POOL
			WritablePacket *p = WritablePacket::pool_allocate(headroom, length, tailroom);
			if (!p)
			return 0;
		# else
			WritablePacket *p = new WritablePacket;
			if (!p)
			return 0;
			p->initialize();
			if (!p->alloc_data(headroom, length, tailroom)) {
			p->_head = 0;
			delete p;
			return 0;
			}
		# endif
			if (data)
			memcpy(p->data(), data, length);
			return p;
		#endif

}

#if CLICK_USERLEVEL || CLICK_MINIOS
/** @brief Create and return a new packet (userlevel).
 * @param data data used in the new packet
 * @param length length of packet
 * @param destructor destructor function
 * @param argument argument to destructor function
 * @param headroom headroom available before the data pointer
 * @param tailroom tailroom available after data + length
 * @return new packet, or null if no packet could be created
 *
 * The packet's data pointer becomes the @a data: the data is not copied
 * into the new packet, rather the packet owns the @a data pointer. When the
 * packet's data is eventually destroyed, either because the packet is
 * deleted or because of something like a push() or full(), the @a
 * destructor will be called as @a destructor(@a data, @a length, @a
 * argument). (If @a destructor is null, the packet data will be freed by
 * <tt>delete[] @a data</tt>.) The packet has zero headroom and tailroom.
 *
 * The returned packet's annotations are cleared and its header pointers are
 * null. */
WritablePacket *
Packet::make(unsigned char *data, uint32_t length,
	     buffer_destructor_type destructor, void* argument, int headroom, int tailroom)
{
#if CLICK_PACKET_USE_DPDK
assert(false); //TODO
#else
# if HAVE_CLICK_PACKET_POOL
    WritablePacket *p = WritablePacket::pool_allocate();
# else
    WritablePacket *p = new WritablePacket;
# endif
    if (p) {
	p->initialize();
	p->_head = data - headroom;
	p->_data = data;
	p->_tail = data + length;
	p->_end = p->_tail + tailroom;
	p->_destructor = destructor;
	p->_destructor_argument = argument;
    }
    return p;
# endif
}

void Packet::empty_destructor(unsigned char *, size_t, void *) {

}

/** @brief Copy the content and annotations of another packet (userlevel).
 * @param source packet
 * @param headroom for the new packet
 */
bool
Packet::copy(Packet* p, int headroom)
{
    if (headroom + p->length() > buffer_length())
        return false;
#if CLICK_PACKET_USE_DPDK
    click_chatter("UNIMPLEMENTED!");
    assert(false);
#else
    _data = _head + headroom;
    memcpy(_data,p->data(),p->length());
    _tail = _data + p->length();
#endif
    copy_annotations(p);
    set_mac_header(p->mac_header() ? data() + p->mac_header_offset() : 0);
    set_network_header(p->network_header() ? data() + p->network_header_offset() : 0);
    if (p->has_transport_header())
        set_transport_header(data() + p->transport_header_offset());
    return true;
}

#endif

//
// UNIQUEIFICATION
//

/** @brief Create a clone of this packet.
 * @arg fast The new clone won't be a shared packet and we won't update the
 * reference count of this packet. The buffer won't be freed in any way and an
 * empty destructor will be set. It is usefull if you won't release this packet
 * before you're sure that the clone will be killed and plan on managing the
 * buffer yourself. This is usefull for pktgen applications where it would
 * be hard to achieve good performances.
 * If the packet is a DPDK packet, it will be referenced as a DPDK packet and
 * the DPDK buffer counter will be updated.
 *
 * @return the cloned packet
 *
 * The returned clone has independent annotations, initially copied from this
 * packet, but shares this packet's data.  shared() returns true for both the
 * packet and its clone.  Returns null if there's no memory for the clone.*/
Packet *
Packet::clone(bool fast)
{
#if CLICK_LINUXMODULE
    struct sk_buff *nskb = skb_clone(skb(), GFP_ATOMIC);
    return reinterpret_cast<Packet *>(nskb);
    
#elif CLICK_PACKET_USE_DPDK
    Packet* p = reinterpret_cast<Packet *>(
        rte_pktmbuf_clone(mb(), DPDKDevice::get_mpool(rte_socket_id())));
    p->copy_annotations(this,true);
    p->shift_header_annotations(buffer(), 0);
    click_chatter("Clone %p %p",this->mb(),p->mb());
    click_chatter("Headroom %d %d",headroom(),p->headroom());
    click_chatter("Tailroom %d %d",tailroom(),p->tailroom());
    click_chatter("Length %d %d",length(),p->length());
    click_chatter("Shared %d %d",shared(),p->shared());
    return p;
#elif CLICK_USERLEVEL || CLICK_BSDMODULE || CLICK_MINIOS
# if CLICK_BSDMODULE
    struct mbuf *m;

    if (this->_m == NULL)
        return 0;

    if (this->_m->m_flags & M_EXT
        && (   this->_m->m_ext.ext_type == EXT_JUMBOP
            || this->_m->m_ext.ext_type == EXT_JUMBO9
            || this->_m->m_ext.ext_type == EXT_JUMBO16)) {
        if ((m = dup_jumbo_m(this->_m)) == NULL)
	    return 0;
    }
    else if ((m = m_dup(this->_m, M_DONTWAIT)) == NULL)
	return 0;
# endif

    // timing: .31-.39 normal, .43-.55 two allocs, .55-.58 two memcpys
# if HAVE_CLICK_PACKET_POOL
    Packet *p = WritablePacket::pool_allocate();
# else
    Packet *p = new WritablePacket; // no initialization
# endif
    if (!p)
	return 0;
    if (unlikely(fast)) {
        p->_use_count = 1;
        p->_head = _head;
        p->_data = _data;
        p->_tail = _tail;
        p->_end = _end;
#if HAVE_DPDK
        if (DPDKDevice::is_dpdk_packet(this)) {
          p->_destructor = DPDKDevice::free_pkt;
          p->_destructor_argument = destructor_argument();
          rte_mbuf_refcnt_update((rte_mbuf*)p->_destructor_argument, 1);
        } else if (data_packet() && DPDKDevice::is_dpdk_packet(data_packet())) {
           p->_destructor = DPDKDevice::free_pkt;
           p->_destructor_argument = data_packet()->destructor_argument();
           rte_mbuf_refcnt_update((rte_mbuf*)p->_destructor_argument, 1);
        } else
#endif
        {
        p->_destructor = empty_destructor;
        }
        p->_data_packet = 0;
    } else {
        Packet* origin = this;
        if (origin->_data_packet)
            origin = origin->_data_packet;
        memcpy(p, this, sizeof(Packet));
        p->_use_count = 1;
        p->_data_packet = origin;
	# if CLICK_USERLEVEL || CLICK_MINIOS
		p->_destructor = 0;
	# else
		p->_m = m;
	# endif
		// increment our reference count because of _data_packet reference
		origin->_use_count++;
    }
    return p;

#endif /* CLICK_LINUXMODULE */
}

WritablePacket *
Packet::expensive_uniqueify(int32_t extra_headroom, int32_t extra_tailroom,
			    bool free_on_failure)
{
    assert(extra_headroom >= (int32_t)(-headroom()) && extra_tailroom >= (int32_t)(-tailroom()));

#if CLICK_LINUXMODULE

    struct sk_buff *nskb = skb();

    // preserve this, which otherwise loses a ref here
    if (!free_on_failure)
        if (!(nskb = skb_clone(nskb, GFP_ATOMIC)))
            return NULL;

    // nskb is now not shared, which psk_expand_head asserts
    if (!(nskb = skb_share_check(nskb, GFP_ATOMIC)))
        return NULL;

    if (pskb_expand_head(nskb, extra_headroom, extra_tailroom, GFP_ATOMIC)) {
        kfree_skb(nskb);
        return NULL;
    }

    // success, so kill the clone from above
    if (!free_on_failure)
        kill();

    return reinterpret_cast<WritablePacket *>(nskb);

#elif CLICK_PACKET_USE_DPDK /* !CLICK_LINUXMODULE */
    struct rte_mbuf *mb = this->mb();
    struct rte_mbuf *nmb = DPDKDevice::get_pkt();
    click_chatter("Expensive uniqueify %p %p, exh = %d, ext = %d",mb,nmb,extra_headroom,extra_tailroom);
    if (!nmb) {
        click_chatter("cannot allocate new pktmbuf");
        if (free_on_failure)
            kill();
        return 0;
    }
    nmb->data_off = mb->data_off + extra_headroom;

    rte_pktmbuf_data_len(nmb) = length();
    rte_pktmbuf_pkt_len(nmb) = length();

    WritablePacket *npkt = reinterpret_cast<WritablePacket *>(nmb);
    memcpy(npkt->buffer(), buffer(), length() + headroom() + tailroom());
    memcpy(npkt->all_anno(), all_anno(), sizeof (AllAnno));

    npkt->shift_header_annotations(buffer(), extra_headroom);

    click_chatter("Headroom %d %d",headroom(),npkt->headroom());
    click_chatter("Tailroom %d %d",tailroom(),npkt->tailroom());
    click_chatter("Length %d %d",length(),npkt->length());
    click_chatter("Shared %d %d",shared(),npkt->shared());
    kill(); // Release old mbuf
    return npkt;
#else /* !CLICK_LINUXMODULE */

    int buffer_length = this->buffer_length();
    WritablePacket* p = WritablePacket::pool_allocate(extra_headroom, buffer_length, extra_tailroom);
    if (!p) {
        if (free_on_failure)
            kill();
        return 0;
    }

    uint8_t *old_head = _head, *old_end = _end;
    int headroom = this->headroom();
    int length = this->length();
    uint8_t* new_head = p->_head;
    uint8_t* new_end = p->_end;
#if HAVE_DPDK_PACKET_POOL
        buffer_destructor_type desc = p->_destructor;
        void* arg = p->_destructor_argument;
#endif
    if (_use_count > 1) {
        memcpy(p, this, sizeof(Packet));

        # if CLICK_USERLEVEL || CLICK_MINIOS
            p->_destructor = 0;
        # else
            p->_m = m;
        # endif
    } else {
        p->_head = NULL;
        p->_data_packet = NULL; //packet from pool_data_allocate can be dirty
        WritablePacket::recycle(p);
        p = (WritablePacket*)this;
    }

    p->_head = new_head;
    p->_data = new_head + headroom + extra_headroom;
    p->_tail = p->_data + length;
    p->_end = new_end;

	# if CLICK_BSDMODULE
		struct mbuf *old_m = _m;
	# endif

    unsigned char *start_copy = old_head + (extra_headroom >= 0 ? 0 : -extra_headroom);
    unsigned char *end_copy = old_end + (extra_tailroom >= 0 ? 0 : extra_tailroom);
    memcpy(p->_head + (extra_headroom >= 0 ? extra_headroom : 0), start_copy, end_copy - start_copy);

    // free old data
    if (_data_packet) {
      _data_packet->kill();
    }
# if CLICK_USERLEVEL || CLICK_MINIOS
    else if (_destructor) {
      _destructor(old_head, old_end - old_head, _destructor_argument);
    } else {
#  if HAVE_NETMAP_PACKET_POOL
      if (NetmapBufQ::is_valid_netmap_buffer(old_head)) {
        NetmapBufQ::local_pool()->insert_p(old_head);
      } else
#  endif
      {
        delete[] old_head;
      }
    }
# if HAVE_DPDK_PACKET_POOL
    p->_destructor = desc;
    p->_destructor_argument = arg;
#  else
    _destructor = 0;
# endif

# elif CLICK_BSDMODULE
    m_freem(old_m); // alloc_data() created a new mbuf, so free the old one
# endif

    p->_use_count = 1;
    p->_data_packet = 0;
    p->shift_header_annotations(old_head, extra_headroom);
    return p;

#endif /* CLICK_LINUXMODULE */
}



#ifdef CLICK_BSDMODULE		/* BSD kernel module */
struct mbuf *
Packet::steal_m()
{
  struct Packet *p;
  struct mbuf *m2;

  p = uniqueify();
  m2 = p->m();

  /* Clear the mbuf from the packet: otherwise kill will MFREE it */
  p->_m = 0;
  p->kill();
  return m2;
}

/*
 * Duplicate a packet by copying data from an mbuf chain to a new mbuf with a
 * jumbo cluster (i.e., contiguous storage).
 */
struct mbuf *
Packet::dup_jumbo_m(struct mbuf *m)
{
  int len = m->m_pkthdr.len;
  struct mbuf *new_m;

  if (len > MJUM16BYTES) {
    click_chatter("warning: cannot allocate jumbo cluster for %d bytes", len);
    return NULL;
  }

  new_m = m_getjcl(M_DONTWAIT, m->m_type, m->m_flags & M_COPYFLAGS,
                   (len <= MJUMPAGESIZE ? MJUMPAGESIZE :
                    len <= MJUM9BYTES   ? MJUM9BYTES   :
                                          MJUM16BYTES));
  if (!new_m) {
    click_chatter("warning: jumbo cluster mbuf allocation failed");
    return NULL;
  }

  m_copydata(m, 0, len, mtod(new_m, caddr_t));
  new_m->m_len = len;
  new_m->m_pkthdr.len = len;

  /* XXX: Only a subset of what m_dup_pkthdr() would copy: */
  new_m->m_pkthdr.rcvif = m->m_pkthdr.rcvif;
# if __FreeBSD_version >= 800000
  new_m->m_pkthdr.flowid = m->m_pkthdr.flowid;
# endif
  new_m->m_pkthdr.ether_vtag = m->m_pkthdr.ether_vtag;

  return new_m;
}
#endif /* CLICK_BSDMODULE */

//
// EXPENSIVE_PUSH, EXPENSIVE_PUT
//

/*
 * Prepend some empty space before a packet.
 * May kill this packet and return a new one.
 */
WritablePacket *
Packet::expensive_push(uint32_t nbytes)
{
  static int chatter = 0;
  if (headroom() < nbytes && chatter < 5) {
    click_chatter("expensive Packet::push; have %d wanted %d",
                  headroom(), nbytes);
    chatter++;
  }
  int extra_headroom = (nbytes + 128) & ~3;
  int extra_tailroom = 0;
  if (extra_headroom <= (int)tailroom())
    extra_tailroom =-extra_headroom;

  if (WritablePacket *q = expensive_uniqueify(extra_headroom, extra_tailroom, true)) {
#ifdef CLICK_LINUXMODULE	/* Linux kernel module */
    __skb_push(q->skb(), nbytes);
#elif CLICK_PACKET_USE_DPDK
    rte_pktmbuf_prepend(q->mb(), nbytes);
    click_chatter("New head : %d",q->headroom());
#else				/* User-space and BSD kernel module */
    q->_data -= nbytes;
# ifdef CLICK_BSDMODULE
    q->m()->m_data -= nbytes;
    q->m()->m_len += nbytes;
    q->m()->m_pkthdr.len += nbytes;
# endif
#endif
    return q;
  } else
    return 0;
}

WritablePacket *
Packet::expensive_put(uint32_t nbytes)
{
#if CLICK_PACKET_USE_DPDK
    assert(false);
#endif
  static int chatter = 0;
  if (tailroom() < nbytes && chatter < 5) {
    click_chatter("expensive Packet::put; have %d wanted %d",
                  tailroom(), nbytes);
    chatter++;
  }
  if (WritablePacket *q = expensive_uniqueify(0, nbytes + 128, true)) {
#ifdef CLICK_LINUXMODULE	/* Linux kernel module */
    __skb_put(q->skb(), nbytes);
#elif CLICK_PACKET_USE_DPDK
    rte_pktmbuf_append(q->mb(), nbytes);
#else				/* User-space and BSD kernel module */
    q->_tail += nbytes;
# ifdef CLICK_BSDMODULE
    q->m()->m_len += nbytes;
    q->m()->m_pkthdr.len += nbytes;
# endif
#endif
    return q;
  } else
    return 0;
}

Packet *
Packet::shift_data(int offset, bool free_on_failure)
{
#if CLICK_PACKET_USE_DPDK
    assert(false);
#endif


    if (offset == 0)
	return this;

    // Preserve mac_header, network_header, and transport_header.
    const unsigned char *dp = data();
    if (has_mac_header() && mac_header() >= buffer()
	&& mac_header() <= end_buffer() && mac_header() < dp)
	dp = mac_header();
    if (has_network_header() && network_header() >= buffer()
	&& network_header() <= end_buffer() && network_header() < dp)
	dp = network_header();
    if (has_transport_header() && transport_header() >= buffer()
	&& transport_header() <= end_buffer() && transport_header() < dp)
	dp = network_header();

    if (!shared()
	&& (offset < 0 ? (dp - buffer()) >= (ptrdiff_t)(-offset)
	    : tailroom() >= (uint32_t)offset)) {
	WritablePacket *q = static_cast<WritablePacket *>(this);
	memmove((unsigned char *) dp + offset, dp, q->end_data() - dp);
#if CLICK_LINUXMODULE
	struct sk_buff *mskb = q->skb();
	mskb->data += offset;
	mskb->tail += offset;
#elif CLICK_PACKET_USE_DPDK
        rte_pktmbuf_adj(q->mb(), offset);
        rte_pktmbuf_append(q->mb(), offset);
#else				/* User-space and BSD kernel module */
	q->_data += offset;
	q->_tail += offset;
# if CLICK_BSDMODULE
	q->m()->m_data += offset;
# endif
#endif
	shift_header_annotations(q->buffer(), offset);
	return this;
    } else {
	int tailroom_offset = (offset < 0 ? -offset : 0);
	if (offset < 0 && headroom() < (uint32_t)(-offset))
	    offset = -headroom() + ((uintptr_t)(data() + offset) & 7);
	else
	    offset += ((uintptr_t)buffer() & 7);
	return expensive_uniqueify(offset, tailroom_offset, free_on_failure);
    }
}

#if HAVE_CLICK_PACKET_POOL
static void
cleanup_pool(PacketPool *pp, int global)
{
    unsigned pcount = 0, pdcount = 0;
    while (WritablePacket *p = pp->p) {
	++pcount;
	pp->p = static_cast<WritablePacket *>(p->next());
	::operator delete((void *) p);
    }
    while (WritablePacket *pd = pp->pd) {
    ++pdcount;
    pp->pd = static_cast<WritablePacket *>(pd->next());
#if HAVE_DPDK_PACKET_POOL
    rte_pktmbuf_free((struct rte_mbuf*)pd->destructor_argument());
#elif HAVE_NETMAP_PACKET_POOL
    NetmapBufQ::local_pool()->insert_p(pd->buffer());
#else
# if HAVE_DPDK
    if (dpdk_enabled)
        rte_free(reinterpret_cast<unsigned char *>(pd->buffer()));
    else
# endif
        delete[] reinterpret_cast<unsigned char *>(pd->buffer());
#endif
    ::operator delete((void *) pd);
    }
#if !HAVE_BATCH_RECYCLE
    assert(pcount <= CLICK_PACKET_POOL_SIZE);
    assert(pdcount <= CLICK_PACKET_POOL_SIZE);
#endif
    assert(global || (pcount == pp->pcount && pdcount == pp->pdcount));
}
#endif

void
Packet::static_cleanup()
{
#if HAVE_CLICK_PACKET_POOL
	# if HAVE_MULTITHREAD
		while (PacketPool* pp = global_packet_pool.thread_pools) {
		global_packet_pool.thread_pools = pp->thread_pool_next;
		cleanup_pool(pp, 0);
		delete pp;
		}

		PacketPool fake_pool;
		do {
			fake_pool.p = global_packet_pool.pbatch.extract();
			fake_pool.pd = global_packet_pool.pdbatch.extract();
			if (!fake_pool.p && !fake_pool.pd) break;
			cleanup_pool(&fake_pool, 1);
		} while(true);
	# else
		cleanup_pool(&global_packet_pool, 0);
	# endif
#endif
}


#if HAVE_STATIC_ANNO
unsigned int Packet::clean_offset = 0;
unsigned int Packet::clean_size = 0;
#endif

CLICK_ENDDECLS
