// -*- c-basic-offset: 4; related-file-name: "../../lib/netmapdevice.cc" -*-
/*
 * netmapdevice.{cc,hh} -- Library to use netmap
 * Eddie Kohler, Luigi Rizzo, Tom Barbette
 *
 * Copyright (c) 2012 Eddie Kohler
 * Copyright (c) 2014-2015 University of Liege
 *
 * NetmapBufQ implementation was started by Luigi Rizzo and moved from
 * netmapinfo.hh.
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
#ifndef CLICK_NETMAPDEVICE_HH
#define CLICK_NETMAPDEVICE_HH

#if HAVE_NETMAP && CLICK_USERLEVEL
#include <net/if.h>
#define __UAPI_DEF_IF_NET_DEVICE_FLAGS 0
#include <linux/if.h>

extern "C" {
#include <net/netmap.h>
#define NETMAP_WITH_LIBS 1
#include <net/netmap_user.h>
}
#define NS_NOFREE 0x80 //We use this to set that a buffer is shared and should not be freed


#include <click/error.hh>
#include <click/ring.hh>
#include <click/vector.hh>
#include <click/hashmap.hh>
#include <click/element.hh>
#include <click/args.hh>
#include <click/packet.hh>
#include <click/sync.hh>

CLICK_DECLS

#define CLICK_NETMAP_POOL_DEBUG 0

#define NETMAP_PACKET_POOL_SIZE			2048
#define BUFFER_PTR(idx) reinterpret_cast<uint32_t *>(buf_start + idx * buf_size)
#define BUFFER_NEXT_LIST(idx) *(((uint32_t*)BUFFER_PTR(idx)) + 1)

#define CLICK_NETMAP_POOL_DEBUG_MAGIC(idx) (*(BUFFER_PTR(idx)+3))
#define CLICK_NETMAP_POOL_DEBUG_MAGIC_VALUE 0b01011100010111000101110001011100

/* a queue of netmap buffers, by index*/
class NetmapBufQ {
public:

	NetmapBufQ();
	~NetmapBufQ();

	inline void expand();

	inline void insert(uint32_t idx);
	inline void insert_p(unsigned char *p);
	inline void insert_all(uint32_t idx, bool check_size);

	inline uint32_t extract();
	inline unsigned char * extract_p();

	inline int count_buffers(uint32_t idx);

	inline int count() const {
		return _count;
	};

	//Static functions
	static int static_initialize(struct nm_desc* nmd);
	static uint32_t static_cleanup();

	static void global_insert_all(uint32_t idx, int count);

	inline static unsigned int buffer_size() {
		return buf_size;
	}

	static void buffer_destructor(unsigned char *buf, size_t, void *) {
		NetmapBufQ::local_pool()->insert_p(buf);
	}

	inline static bool is_netmap_packet(Packet* p) {
		return (p->buffer_destructor() == buffer_destructor ||
				(p->buffer() > NetmapBufQ::buf_start && p->buffer() < NetmapBufQ::buf_end));
	}

	inline static bool is_valid_netmap_buffer(void* data) {
		return (data > NetmapBufQ::buf_start && data < NetmapBufQ::buf_end);
	}

	inline static bool is_valid_netmap_packet(Packet* p) {
		return (p->buffer() > NetmapBufQ::buf_start && p->buffer() < NetmapBufQ::buf_end);
	}

	inline static NetmapBufQ* local_pool() {
		return NetmapBufQ::netmap_buf_pools[click_current_cpu_id()];
	}

	static inline bool initialized() {
		return netmap_buf_pools != 0;
	}

	bool check_list() {
		return find_count(_head) == _count;
	}

	static int find_count(uint32_t cur,int limit = 65536) {
		int c = 0;

		while (cur > 0) {
			uint32_t* p = BUFFER_PTR(cur);
			cur = *p;
			c++;
			assert(c <= limit);
		}
		return c;
	}
private :
	uint32_t _head;  /* index of first buffer */
	int _count; /* how many ? */

	//Static attributes (shared between all queues)
	static unsigned char *buf_start;   /* base address */
	static unsigned char *buf_end; /* error checking */
	static unsigned int buf_size;
	static uint32_t max_index; /* error checking */

	static Spinlock global_buffer_lock;
	//The global netmap buffer list is used to exchange batch of buffers between threads
	//The second uint32_t in the buffer is used to point to the next batch
	static uint32_t global_buffer_list;

	static int messagelimit;
	static NetmapBufQ** netmap_buf_pools;

}  __attribute__((aligned(64)));

/**
 * A Netmap interface, its global descriptor and one descriptor per queue
 */
class NetmapDevice {
	public:

	NetmapDevice(String ifname) CLICK_COLD;
	~NetmapDevice() CLICK_COLD;

	int initialize() CLICK_COLD;
	void destroy() CLICK_COLD;

	atomic_uint32_t n_refs;

	struct nm_desc* parent_nmd;
	Vector<struct nm_desc*> nmds;

	String ifname;
	int n_queues;

#if HAVE_NETMAP_PACKET_POOL
    static WritablePacket* make_netmap(unsigned char* data, struct netmap_ring* rxring, struct netmap_slot* slot);
# if HAVE_BATCH
    static PacketBatch* make_netmap_batch(unsigned int n, struct netmap_ring* rxring, unsigned int &cur);
# endif
#endif
	static NetmapDevice* open(String ifname);
	static void static_cleanup();
	static struct nm_desc* some_nmd;
	static int global_alloc;

	int _minfd;
	int _maxfd;

	int get_num_slots() {
		return some_nmd->some_ring->num_slots;
	}

private :
	static HashMap<String,NetmapDevice*> nics;

	int _use_count;
};

/*
 * Inline functions
 */

inline void NetmapBufQ::expand() {
	global_buffer_lock.acquire();
	if (global_buffer_list != 0) {
		//Transfer from global pool
		_head = global_buffer_list;
		global_buffer_list = BUFFER_NEXT_LIST(global_buffer_list);
		_count = NETMAP_PACKET_POOL_SIZE;
	} else {
#ifdef NIOCALLOCBUF
		click_chatter("Expanding buffer pool with %d new packets",NETMAP_PACKET_POOL_SIZE);
		struct nmbufreq nbr;
		nbr.num = NETMAP_PACKET_POOL_SIZE;
		nbr.head = 0;
		if (ioctl(NetmapDevice::some_nmd->fd,NIOCALLOCBUF,&nbr) == 0) {
			insert_all(nbr.head,false);
		} else
#endif
		{
		if (messagelimit < 5)
			click_chatter("No more netmap buffers !");
		messagelimit++;
}
	}
	global_buffer_lock.release();
}

/**
 * Insert a list of netmap buffers in the queue
 */
inline void NetmapBufQ::insert_all(uint32_t idx,bool check_size = false) {
	if (unlikely(idx >= max_index || idx == 0)) {
		click_chatter("Error : cannot insert index %d",idx);
		return;
	}

	uint32_t firstidx = idx;
	uint32_t *p = 0;
	while (idx > 0) { //Go to the end of the passed list
		if (check_size) {
			insert(idx);
		} else {
			p = BUFFER_PTR(idx);
			idx = *p;
			_count++;
		}
	}

	//Add the current list at the end of this one
	if (!check_size) {
		*p = _head;
		_head = firstidx;
	}
}

/**
 * Return the number of buffer inside a netmap buffer list
 */
int NetmapBufQ::count_buffers(uint32_t idx) {
	int count=0;
	while (idx != 0) {
		count++;
		idx = *BUFFER_PTR(idx);
	}
	return count;
}

inline void NetmapBufQ::insert(uint32_t idx) {
#if CLICK_NETMAP_POOL_DEBUG
	assert(idx > 0 && idx < max_index);
	assert(CLICK_NETMAP_POOL_DEBUG_MAGIC(idx) != CLICK_NETMAP_POOL_DEBUG_MAGIC_VALUE);
#endif

	if (_count < NETMAP_PACKET_POOL_SIZE) {
		*BUFFER_PTR(idx) = _head;
		_head = idx;
		_count++;
	} else {
#if CLICK_NETMAP_POOL_DEBUG
		assert(_count == NETMAP_PACKET_POOL_SIZE);
		assert(find_count(_head) == NETMAP_PACKET_POOL_SIZE);
#endif
		global_buffer_lock.acquire();
		BUFFER_NEXT_LIST(_head) = global_buffer_list;
		global_buffer_list = _head;
		global_buffer_lock.release();
		_head = idx;
		*BUFFER_PTR(idx) = 0;
		_count = 1;
	}
}

inline void NetmapBufQ::insert_p(unsigned char* buf) {
	insert((buf - buf_start) / buf_size);
}

inline uint32_t NetmapBufQ::extract() {
	if (_count <= 0) {
		expand();
		if (_count == 0) return 0;
	}
	uint32_t idx;
	uint32_t *p;
	idx = _head;
	p  = BUFFER_PTR(idx);
	_head = *p;
	_count--;
	#if CLICK_NETMAP_POOL_DEBUG
	CLICK_NETMAP_POOL_DEBUG_MAGIC(idx) = 0;
	#endif
	return idx;
}

inline unsigned char* NetmapBufQ::extract_p() {
	uint32_t idx = extract();
	return (idx == 0) ? 0 : reinterpret_cast<unsigned char*>(BUFFER_PTR(idx));
}

#endif

#endif
