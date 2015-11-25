/*
 * todpdkdevice.{cc,hh} -- element sends packets to network via Intel's DPDK
 *
 * Copyright (c) 2014-2015 Cyril Soldani, University of Liège
 * Copyright (c) 2015 Tom Barbette, University of Liège
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

#include <rte_ethdev.h>
#include <rte_mbuf.h>

#include "todpdkdevice.hh"

CLICK_DECLS

ToDPDKDevice::ToDPDKDevice() :
    _iqueues(), _port_id(0), _blocking(false),
    _iqueue_size(1024), _burst_size(32), _timeout(0), _congestion_warning_printed(false)
{
}

ToDPDKDevice::~ToDPDKDevice()
{
}

int ToDPDKDevice::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String devname;
    int maxthreads = -1;
    int minqueues = 1;
    int maxqueues = 128;
    int burst = -1;

    if (Args(conf, this, errh)
        .read_mp("PORT", _port_id)
        .read("IQUEUE", _iqueue_size)
        .read("MAXQUEUES",maxqueues)
        .read("MAXTHREADS",maxthreads)
        .read("BLOCKING", _blocking)
        .read("BURST", burst)
        .read("MAXQUEUES",maxqueues)
        .read("TIMEOUT", _timeout)
        .read("NDESC",_n_desc)
        .complete() < 0)
            return -1;


#if !HAVE_BATCH
    if (burst > ndesc / 2 ) {
        errh->warning("BURST should not be upper than half the number of descriptor");
    } else if (burst > 32) {
        errh->warning("BURST should not be upper than 32 as DPDK won't send more packets at once");
    }
    if (burst > -1)
        _burst = burst;

#else
    if (burst != -1) {
        errh->warning("BURST is unused with batching !");
    }
#endif

    QueueDevice::configure_tx(maxthreads,1,maxqueues,errh);
}

int ToDPDKDevice::initialize(ErrorHandler *errh)
{
    int ret;

    ret = initialize_tx(errh);
    if (ret != 0)
        return ret;

    for (int i = 0; i < nqueues; i++) {
        ret = DPDKDevice::add_tx_device(_port_id, i, _n_desc , errh);
        if (ret != 0) return ret;    }

    ret = initialize_tasks(false,errh);
    if (ret != 0)
        return ret;

    for (int i = 0; i < _iqueues.size();i++) {
    	_iqueues.get_value(i).pkts = new struct rte_mbuf *[_iqueue_size];
        if (_timeout >= 0) {
            _iqueues.get_value(i).timeout.assign(this);
            _iqueues.get_value(i).timeout.initialize(this);
        }
    }

    if (all_initialized()) {
        int ret =DPDKDevice::initialize(errh);
        if (ret != 0) return ret;
    }
    return 0;
}

void ToDPDKDevice::cleanup(CleanupStage stage)
{
	cleanup_tasks();
	for (int i = 0; i < _iqueues.size();i++) {
			delete[] _iqueues.get_value(i).pkts;
	}
}

void ToDPDKDevice::add_handlers()
{
    add_read_handler("n_sent", count_handler, 0);
    add_read_handler("n_dropped", dropped_handler, 0);
    add_write_handler("reset_counts", reset_count_handler, 0, Handler::BUTTON);
}

inline struct rte_mbuf* get_mbuf(Packet* p, bool create=true) {
    struct rte_mbuf* mbuf;
    #if CLICK_DPDK_POOLS
    mbuf = p->mb();
    #else
    if (likely(DPDKDevice::is_dpdk_packet(p) && !p->shared())) {
        mbuf = (struct rte_mbuf *) (p->buffer() - sizeof(struct rte_mbuf));
        rte_pktmbuf_pkt_len(mbuf) = p->length();
        rte_pktmbuf_data_len(mbuf) = p->length();
        static_cast<WritablePacket*>(p)->set_buffer(0);
    } else {
        if (create) {
            mbuf = DPDKDevice::get_pkt();
            memcpy((void*)rte_pktmbuf_mtod(mbuf, unsigned char *),p->data(),p->length());
            rte_pktmbuf_pkt_len(mbuf) = p->length();
            rte_pktmbuf_data_len(mbuf) = p->length();
        } else
            return NULL;
    }
    #endif
    return mbuf;
}

void ToDPDKDevice::run_timer(Timer *)
{
    flush_internal_queue(_iqueues.get());
}


/* Flush as much as possible packets from a given internal queue to the DPDK
 * device. */
void ToDPDKDevice::flush_internal_queue(InternalQueue &iqueue) {
    unsigned sent = 0;
    unsigned r;
    /* sub_burst is the number of packets DPDK should send in one call if
     * there is no congestion, normally 32. If it sends less, it means
     * there is no more room in the output ring and we'll need to come
     * back later. Also, if we're wrapping around the ring, sub_burst
     * will be used to split the burst in two, as rte_eth_tx_burst needs a
     * contiguous buffer space.
     */
    unsigned sub_burst;

    lock();

    do {
        sub_burst = iqueue.nr_pending > 32 ? 32 : iqueue.nr_pending;
        if (iqueue.index + sub_burst >= _iqueue_size)
            // The sub_burst wraps around the ring
            sub_burst = _iqueue_size - iqueue.index;
        r = rte_eth_tx_burst(_port_id, queue_for_thread_begin(), &iqueue.pkts[iqueue.index],
                             iqueue.nr_pending);

        iqueue.nr_pending -= r;
        iqueue.index += r;

        if (iqueue.index >= _iqueue_size) // Wrapping around the ring
            iqueue.index = 0;

        sent += r;
    } while (r == sub_burst && iqueue.nr_pending > 0);
    unlock();

    add_count(sent);

    // If ring is empty, reset the index to avoid wrap ups
    if (iqueue.nr_pending == 0)
        iqueue.index = 0;
}

void ToDPDKDevice::push(int, Packet *p)
{
    // Get the thread-local internal queue
    InternalQueue &iqueue = _iqueues.get();

    bool congestioned;
    do {
        congestioned = false;

        if (iqueue.nr_pending == _iqueue_size) { // Internal queue is full
            /* We just set the congestion flag. If we're in blocking mode,
             * we'll loop, else we'll drop this packet.*/
            congestioned = true;
            if (!_blocking) {
                if (!_congestion_warning_printed)
                    click_chatter("%s: packet dropped", name().c_str());
                _congestion_warning_printed = true;
            } else {
                if (!_congestion_warning_printed)
                    click_chatter("%s: congestion warning", name().c_str());
                _congestion_warning_printed = true;
            }
        } else { // If there is space in the iqueue
            iqueue.pkts[(iqueue.index + iqueue.nr_pending) % _iqueue_size] =
                get_mbuf(p);
            iqueue.nr_pending++;
        }

        if (iqueue.nr_pending >= _burst_size || congestioned) {
            flush_internal_queue(iqueue);
            if (_timeout && iqueue.nr_pending == 0)
                iqueue.timeout.unschedule();
        } else if (_timeout >= 0 && !iqueue.timeout.scheduled()) {
            if (_timeout == 0)
                iqueue.timeout.schedule_now();
            else
                iqueue.timeout.schedule_after_msec(_timeout);
        }

        // If we're in blocking mode, we loop until we can put p in the iqueue
    } while (unlikely(_blocking && congestioned));

#if !CLICK_DPDK_POOLS
	if (likely(is_fullpush()))
	    p->safe_kill();
	else
	    p->kill();
#endif
}


/**
 * push_batch seems more complex than in tonetmapdevice, but it's only because
 *  we have to place pointers in an array, and we don't want to keep a linked
 *  list plus an array (we could end up with packets which were not sent in the
 *  array, and packets in the list, it would be a mess). So we use an array as
 *  a ring and it produce multiple "bad cases".
 */
#if HAVE_BATCH
void ToDPDKDevice::push_batch(int, PacketBatch *head)
{
	// Get the thread-local internal queue
	InternalQueue &iqueue = _iqueues.get();

	Packet* p = head;

#if HAVE_BATCH_RECYCLE
	    BATCH_RECYCLE_START();
#endif

	struct rte_mbuf **pkts = iqueue.pkts;

	if (iqueue.nr_pending) {
		//TODO : why never more than 32?
		unsigned ret = 0;
		unsigned r;
		unsigned left = iqueue.nr_pending;
		do {
			lock();
			r = rte_eth_tx_burst(_port_id, queue_for_thread_begin(), &iqueue.pkts[iqueue.index + ret] , left);
			unlock();
			ret += r;
			left -= r;
		} while (r == 32 && left > 0);

		if (ret == iqueue.nr_pending) {//all was sent
		    iqueue.nr_pending = 0;
			iqueue.index = 0;
			//Reset, there is nothing in the internal queue
		} else if (iqueue.index + iqueue.nr_pending + head->count() <  _iqueue_size) {
			//Place the new packets after the old
		    iqueue.index += ret;
		    iqueue.nr_pending -= ret;
			pkts = &iqueue.pkts[iqueue.index + iqueue.nr_pending];
		} else if ((int)iqueue.index + (int)ret - (int)head->count() >= (int)0) {
			//Place the new packets before the older
		   // click_chatter("Before !");
			iqueue.index = (unsigned int)((int)iqueue.index - (int)head->count() + (int)ret);
			iqueue.nr_pending -= ret;
			pkts = &iqueue.pkts[iqueue.index];
		} else {
		    //Drop packets

			unsigned int lost = iqueue.nr_pending - ret;
			add_dropped(lost);
			//click_chatter("Dropped %d");
			for (int i = iqueue.index + ret; i < iqueue.index + iqueue.nr_pending; i++) {
				rte_pktmbuf_free(iqueue.pkts[i]);
			}

			iqueue.index = 0;
			iqueue.nr_pending = 0;
			//Reset, we will erase the old
		}

	}

	struct rte_mbuf **pkt = pkts;

	while (p != NULL) {
        *pkt = get_mbuf(p);
#if HAVE_BATCH_RECYCLE
			//We cannot recycle a batch with shared packet in it
			if (p->shared()) {
				p->kill();
			} else {
				if (!p->buffer()) {
					BATCH_RECYCLE_PACKET(p);
				} else {
					BATCH_RECYCLE_UNKNOWN_PACKET(p);
				}
			}
#else
#endif
		pkt++;
		p = p->next();
	}

	unsigned ret = 0;
	unsigned r;
	unsigned left = head->count() + iqueue.nr_pending;
	do {
	    lock();
		r = rte_eth_tx_burst(_port_id, queue_for_thread_begin(), &iqueue.pkts[iqueue.index + ret] , left);
		unlock();
		ret += r;
		left -= r;
	} while (r == 32 && left > 0);
	add_count(ret);
	if (ret == head->count() + iqueue.nr_pending) { //All was sent
	    iqueue.index = 0;
	    iqueue.nr_pending = 0;
	} else {
	    iqueue.index = iqueue.index + ret;
	    iqueue.nr_pending = head->count() + iqueue.nr_pending - ret;
	}

#if !CLICK_DPDK_POOLS
	#if HAVE_BATCH_RECYCLE
		BATCH_RECYCLE_END();
#else
	 head->kill();
#endif

#endif

}
#endif

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel dpdk)
EXPORT_ELEMENT(ToDPDKDevice)
