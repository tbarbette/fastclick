/*
 * todpdkring.{cc,hh} -- element that sends packets to a circular ring buffer using DPDK.
 * Georgios Katsikas
 *
 * Copyright (c) 2016 KTH Royal Institute of Technology
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
#include <click/args.hh>
#include <click/error.hh>

#include "todpdkring.hh"

CLICK_DECLS

ToDPDKRing::ToDPDKRing() :
	_message_pool(0), _send_ring(0), _iqueue(),
	_numa_zone(0), _iqueue_size(1024),
	_burst_size(0), _timeout(0),
	_blocking(false), _congestion_warning_printed(false),
	_n_sent(0), _n_dropped(0)
{
	_ndesc = DPDKDevice::DEF_RING_NDESC;
	_def_burst_size = DPDKDevice::DEF_BURST_SIZE;
}

ToDPDKRing::~ToDPDKRing()
{
}

int
ToDPDKRing::configure(Vector<String> &conf, ErrorHandler *errh)
{
	if (Args(conf, this, errh)
		.read_mp("MEM_POOL",  _MEM_POOL)
		.read_mp("FROM_PROC", _origin)
		.read_mp("TO_PROC",   _destination)
		.read("IQUEUE",       _iqueue_size)
		.read("BLOCKING",     _blocking)
		.read("BURST",        _burst_size)
		.read("NDESC",        _ndesc)
		.read("TIMEOUT",      _timeout)
		.read("NUMA_ZONE",    _numa_zone)
		.complete() < 0)
			return -1;

	if ( _MEM_POOL.empty() || (_MEM_POOL.length() == 0) ) {
		return errh->error("[%s] Enter FROM_PROC and TO_PROC names", name().c_str());
	}

	if ( _origin.empty() || _destination.empty() ) {
		return errh->error("[%s] Enter FROM_PROC and TO_PROC names", name().c_str());
	}

	if ( _ndesc == 0 ) {
		_ndesc = DPDKDevice::DEF_RING_NDESC;
		click_chatter("[%s] Default number of descriptors is set (%d)\n",
						name().c_str(), _ndesc);
	}

	_MEM_POOL = DPDKDevice::MEMPOOL_PREFIX + _MEM_POOL;

	// If user does not specify the port number
	// we assume that the process belongs to the
	// memory zone of device 0.
	if ( _numa_zone < 0 ) {
		click_chatter("[%s] Assuming NUMA zone 0\n");
		_numa_zone = 0;
	}

	_PROC_1 = _origin+"_2_"+_destination;
	_PROC_2 = _destination+"_2_"+_origin;

	return 0;
}

int
ToDPDKRing::initialize(ErrorHandler *errh)
{
#if HAVE_BATCH
	if (batch_mode() == BATCH_MODE_YES) {
		if ( _burst_size > 0 )
			errh->warning("[%s] BURST is unused with batching!", name().c_str());
	} else
#endif
	{
		if ( _burst_size == 0 ) {
			_burst_size = _def_burst_size;
			click_chatter("[%s] Non-positive BURST number. Setting default (%d) \n",
							name().c_str(), _burst_size);
		}

		if ( (_ndesc > 0) && ((unsigned)_burst_size > _ndesc / 2) ) {
			errh->warning("[%s] BURST should not be greater than half the number of descriptors (%d)",
							name().c_str(), _ndesc);
		}
		else if ( _burst_size > _def_burst_size ) {
			errh->warning("[%s] BURST should not be greater than 32 as DPDK won't send more packets at once",
							name().c_str());
		}
	}

	// If primary process, create the ring buffer and memory pool.
	// The primary process is responsible for managing the memory
	// and acting as a bridge to interconnect various secondary processes
	if ( rte_eal_process_type() == RTE_PROC_PRIMARY ){
		_send_ring = rte_ring_create(
			_PROC_1.c_str(), DPDKDevice::RING_SIZE,
			rte_socket_id(), DPDKDevice::RING_FLAGS
		);
		
		_message_pool = rte_mempool_create(
			_MEM_POOL.c_str(), _ndesc,
			DPDKDevice::MBUF_DATA_SIZE,
			DPDKDevice::RING_POOL_CACHE_SIZE,
			DPDKDevice::RING_PRIV_DATA_SIZE,
			NULL, NULL, NULL, NULL,
			rte_socket_id(), DPDKDevice::RING_FLAGS
		);
	}
	// If secondary process, search for the appropriate memory and attach to it.
	else {
		_send_ring    = rte_ring_lookup   (_PROC_2.c_str());
		_message_pool = rte_mempool_lookup(_MEM_POOL.c_str());
	}

	if ( !_send_ring )
		return errh->error("[%s] Problem getting Tx ring. "
							"Make sure that the process attached to this element has the right ring configuration\n",
							name().c_str());
	if ( !_message_pool )
		return errh->error("[%s] Problem getting message pool. "
							"Make sure that the process attached to this element has the right ring configuration\n",
							name().c_str());

	// Initialize the internal queue
	_iqueue.pkts = new struct rte_mbuf *[_iqueue_size];
	if (_timeout >= 0) {
		_iqueue.timeout.assign     (this);
		_iqueue.timeout.initialize (this);
		_iqueue.timeout.move_thread(click_current_cpu_id());
	}

	/*
	click_chatter("[%s] Initialized with the following options: \n", name().c_str());
	click_chatter("|->  MEM_POOL: %s \n", _MEM_POOL.c_str());
	click_chatter("|-> FROM_PROC: %s \n", _origin.c_str());
	click_chatter("|->   TO_PROC: %s \n", _destination.c_str());
	click_chatter("|-> NUMA ZONE: %d \n", _numa_zone);
	click_chatter("|->    IQUEUE: %d \n", _iqueue_size);
	click_chatter("|->     BURST: %d \n", _burst_size);
	click_chatter("|->     NDESC: %d \n", _ndesc);
	click_chatter("|->   TIMEOUT: %d \n", _timeout);
	*/

	return 0;
}

void
ToDPDKRing::cleanup(CleanupStage stage)
{
	if ( _iqueue.pkts )
		delete[] _iqueue.pkts;
}

inline struct rte_mbuf*
ToDPDKRing::get_mbuf(Packet* p, bool create=true)
{
	struct rte_mbuf* mbuf;
#if CLICK_PACKET_USE_DPDK
	mbuf = p->mb();
#else

	if ( likely(DPDKDevice::is_dpdk_packet(p) && (mbuf = (struct rte_mbuf *) p->destructor_argument())) ||
		 unlikely(p->data_packet() && DPDKDevice::is_dpdk_packet(p->data_packet()) && 
		 (mbuf = (struct rte_mbuf *) p->data_packet()->destructor_argument())))
	{
		// If the packet is an unshared DPDK packet, we can send the mbuf as it to DPDK
		rte_pktmbuf_pkt_len(mbuf) = p->length();
		rte_pktmbuf_data_len(mbuf) = p->length();
		mbuf->data_off = p->headroom();
		if (p->shared()) {
			// Prevent DPDK from freeing the buffer. When all shared packet are freed,
			// DPDKDevice::free_pkt will effectively destroy it.
			rte_mbuf_refcnt_update(mbuf, 1);
		}
		else {
			//Reset buffer, let DPDK free the buffer when it wants
			p->reset_buffer();
		}
	}
	else {
		if (create) {
			// The packet is not a DPDK packet, or it is shared : we need to
			// allocate an mbuf and copy the packet content to it.
			mbuf = DPDKDevice::get_pkt(_numa_zone);
			if (mbuf == 0) {
				click_chatter(
					"[%s] Out of DPDK buffer! Check your configuration for "
					"packet leaks or increase the buffer size with DPDKInfo().", name().c_str()
				);
				return NULL;
			}
			memcpy((void*)rte_pktmbuf_mtod(mbuf, unsigned char *),p->data(),p->length());
			rte_pktmbuf_pkt_len(mbuf) = p->length();
			rte_pktmbuf_data_len(mbuf) = p->length();
		}
		else
			return NULL;
	}
#endif

	return mbuf;
}

void
ToDPDKRing::run_timer(Timer *)
{
	flush_internal_queue(_iqueue);
}


/* Flush as many packets as possible from the internal queue of the DPDK ring. */
void
ToDPDKRing::flush_internal_queue(InternalQueue &iqueue)
{
	unsigned n;
	unsigned sent = 0;
	/*
	 * sub_burst is the number of packets DPDK should send in one call if
	 * there is no congestion, normally 32. If it sends less, it means
	 * there is no more room in the output ring and we'll need to come
	 * back later. Also, if we're wrapping around the ring, sub_burst
	 * will be used to split the burst in two, as rte_eth_tx_burst needs a
	 * contiguous buffer space.
	 */
	unsigned sub_burst;

	do {
		sub_burst = iqueue.nr_pending > _def_burst_size ?
			_def_burst_size : iqueue.nr_pending;

		// The sub_burst wraps around the ring
		if (iqueue.index + sub_burst >= _iqueue_size)
			sub_burst = _iqueue_size - iqueue.index;
		
		n = rte_ring_enqueue_burst(
			_send_ring, (void* const*)(&iqueue.pkts[iqueue.index]),
			sub_burst
		);

		iqueue.nr_pending -= n;
		iqueue.index      += n;

		// Wrapping around the ring
		if (iqueue.index >= _iqueue_size)
			iqueue.index = 0;

		sent += n;
	} while ( (n == sub_burst) && (iqueue.nr_pending > 0) );

	_n_sent += sent;

	// If ring is empty, reset the index to avoid wrap ups
	if (iqueue.nr_pending == 0)
		iqueue.index = 0;
}

void
ToDPDKRing::push_packet(int, Packet *p)
{
	// Get the thread-local internal queue
	InternalQueue &iqueue = _iqueue;

	bool congestioned;
	do {
		congestioned = false;

		// Internal queue is full
		if (iqueue.nr_pending == _iqueue_size) {
			// We just set the congestion flag. If we're in blocking mode,
			// we'll loop, else we'll drop this packet.
			congestioned = true;

			if (!_blocking) {
				if ( !_congestion_warning_printed )
					click_chatter("[%s] Packet dropped", name().c_str());
				_congestion_warning_printed = true;
			}
			else {
				if ( !_congestion_warning_printed )
					click_chatter("[%s] Congestion warning", name().c_str());
				_congestion_warning_printed = true;
			}
		}
		// There is space in the iqueue
		else {
			struct rte_mbuf* mbuf = get_mbuf(p);
			if ( mbuf ) {
				iqueue.pkts[(iqueue.index + iqueue.nr_pending) % _iqueue_size] = mbuf;
				iqueue.nr_pending++;
			}
		}

		if ( (int)iqueue.nr_pending >= _burst_size || congestioned ) {
			flush_internal_queue(iqueue);
			if (_timeout && iqueue.nr_pending == 0)
				iqueue.timeout.unschedule();
		}
		else if (_timeout >= 0 && !iqueue.timeout.scheduled()) {
			if (_timeout == 0)
				iqueue.timeout.schedule_now();
			else
				iqueue.timeout.schedule_after_msec(_timeout);
		}
		// If we're in blocking mode, we loop until we can put p in the iqueue
	} while (unlikely(_blocking && congestioned));

#if !CLICK_PACKET_USE_DPDK
	if ( likely(is_fullpush()) )
		p->safe_kill();
	else
		p->kill();
#endif
}


#if HAVE_BATCH
void ToDPDKRing::push_batch(int, PacketBatch *head)
{
	// Get the internal queue
	InternalQueue &iqueue = _iqueue;
	// .. and a pointer to the first packet of the batch
	Packet* p = head;

#if HAVE_BATCH_RECYCLE
		BATCH_RECYCLE_START();
#endif

	struct rte_mbuf **pkts = iqueue.pkts;

	/////////////////////////////////////////////////////
	// There are packets in the queue
	/////////////////////////////////////////////////////
	if ( iqueue.nr_pending ) {
		unsigned ret  = 0;
		unsigned n    = 0;
		unsigned left = iqueue.nr_pending;
		do {
			n = rte_ring_enqueue_burst(_send_ring, (void* const*)(&iqueue.pkts[iqueue.index + ret]), left);
			ret  += n;
			left -= n;
		} while ( (n == _def_burst_size) && (left > 0) );

		// All sent
		if ( ret == iqueue.nr_pending ) {
			//Reset, there is nothing in the internal queue
			iqueue.index      = 0;
			iqueue.nr_pending = 0;
		}
		// Place the new packets after the old
		else if ( iqueue.index + iqueue.nr_pending + head->count() <  _iqueue_size ) {
			iqueue.index      += ret;
			iqueue.nr_pending -= ret;
			pkts = &iqueue.pkts[iqueue.index + iqueue.nr_pending];
		}
		//Place the new packets before the older
		else if ( (int)iqueue.index + (int)ret - (int)head->count() >= (int)0 ) {
			iqueue.index       = (unsigned int)((int)iqueue.index - (int)head->count() + (int)ret);
			iqueue.nr_pending -= ret;
			pkts = &iqueue.pkts[iqueue.index];
		}
		// Drop packets
		else {
			unsigned int lost = iqueue.nr_pending - ret;
			_n_dropped += lost;

			for (unsigned i = iqueue.index + ret; i < iqueue.index + iqueue.nr_pending; i++) {
				rte_pktmbuf_free(iqueue.pkts[i]);
			}

			//Reset, we will erase the old
			iqueue.index      = 0;
			iqueue.nr_pending = 0;
		}
		_n_sent += ret;
	}

	/////////////////////////////////////////////////////
	// After dealing with the remaining packets above (if any),
	// let's now construct a new batch that incorporates
	// the newly arrived ones.
	/////////////////////////////////////////////////////
	struct rte_mbuf **new_pkts = pkts;
	while ( p ) {
		Packet* next = p->next();
		*new_pkts = get_mbuf(p);
		if (*new_pkts == 0)
			break;
	#if !CLICK_PACKET_USE_DPDK
		BATCH_RECYCLE_UNSAFE_PACKET(p);
	#endif
		new_pkts++;
		p = next;
	}

	/////////////////////////////////////////////////////
	// Time to transmit this new batch
	/////////////////////////////////////////////////////
	unsigned ret  = 0;
	unsigned n    = 0;
	unsigned left = head->count() + iqueue.nr_pending;
	do {
		n = rte_ring_enqueue_burst(_send_ring, (void* const*)&iqueue.pkts[iqueue.index + ret], left);
		ret  += n;
		left -= n;
	} while ( (n == _def_burst_size) && (left > 0) );

	// All sent
	if ( ret == head->count() + iqueue.nr_pending ) {
		iqueue.index      = 0;
		iqueue.nr_pending = 0;
	}
	// Still leftovers
	else {
		iqueue.index      = iqueue.index + ret;
		iqueue.nr_pending = head->count() + iqueue.nr_pending - ret;
	}

	_n_sent += ret;

#if !CLICK_PACKET_USE_DPDK
	#if HAVE_BATCH_RECYCLE
		BATCH_RECYCLE_END();
	#else
		 head->kill();
	#endif
#endif

}
#endif

String
ToDPDKRing::read_handler(Element* e, void *thunk)
{
	ToDPDKRing* tr = static_cast<ToDPDKRing*>(e);

	if ( thunk == (void *) 0 )
		return String(tr->_n_sent);
	else
		return String(tr->_n_dropped);
}

void
ToDPDKRing::add_handlers()
{
	add_read_handler("n_sent",    read_handler, 0);
	add_read_handler("n_dropped", read_handler, 1);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel dpdk)
EXPORT_ELEMENT(ToDPDKRing)
ELEMENT_MT_SAFE(ToDPDKRing)
