/*
 * todpdkdevice.{cc,hh} -- element sends packets to network via Intel's DPDK
 *
 * Copyright (c) 2014-2015 University of Liège
 * Copyright (c) 2014 Cyril Soldani
 * Copyright (c) 2015 Tom Barbette
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

#define N_SENT 0
#define N_DROPPED 1

ToDpdkDevice::ToDpdkDevice() :
    _port_no(0), _internal_queue(1024), _burst(32)
{
}

ToDpdkDevice::~ToDpdkDevice()
{
}

int ToDpdkDevice::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String devname;
    int maxthreads = -1;
    int minqueues = 1;
    int maxqueues = 128;
    int burst = -1;

    if (Args(conf, this, errh)
	.read_mp("DEVNAME", _port_no)
	.read_p("IQUEUE", _internal_queue)
	.read_p("MAXQUEUES",maxqueues)
	.read_p("MAXTHREADS",maxthreads)
	.read_p("BURST", burst)
	.read("MAXQUEUES",maxqueues)
	.read("NDESC",ndesc)
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

int ToDpdkDevice::initialize(ErrorHandler *errh)
{
    int ret;

    ret = initialize_tx(errh);
    if (ret != 0)
        return ret;

    for (int i = 0; i < nqueues; i++) {
        ret = DpdkDevice::add_tx_device(_port_no, i , errh);
        if (ret != 0) return ret;    }

    if (ndesc > 0)
        DpdkDevice::set_tx_descs(_port_no, ndesc, errh);

    ret = initialize_tasks(false,errh);
    if (ret != 0)
        return ret;

    for (int i = 0; i < state.size();i++) {
    	state.get_value(i).glob_pkts = new struct rte_mbuf *[_internal_queue];
    }


    if (all_initialized()) {
        int ret =DpdkDevice::initialize(errh);
        if (ret != 0) return ret;
    }
    return 0;
}

void ToDpdkDevice::cleanup(CleanupStage stage)
{
	cleanup_tasks();
	for (int i = 0; i < state.size();i++) {
		delete[] state.get_value(i).glob_pkts;
	}
}

void ToDpdkDevice::add_handlers()
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
    if (likely(DpdkDevice::is_dpdk_packet(p))) {
        mbuf = (struct rte_mbuf *) p->destructor_argument();
        p->set_buffer_destructor(DpdkDevice::fake_free_pkt);
    } else {
        if (create) {
            mbuf = rte_pktmbuf_alloc(DpdkDevice::get_mpool(rte_socket_id()));
            memcpy((void*)rte_pktmbuf_mtod(mbuf, unsigned char *),p->data(),p->length());
            rte_pktmbuf_pkt_len(mbuf) = p->length();
            rte_pktmbuf_data_len(mbuf) = p->length();
        } else
            return NULL;
    }
    #endif
    return mbuf;
}

void ToDpdkDevice::push(int, Packet *p)
{
	bool drop = false;
	if (state->_int_index + state->_int_left >= _internal_queue) {
		if (state->_int_index > 0) {
			state->_int_index--;
			state->_int_left++;
			state->glob_pkts[state->_int_index] = get_mbuf(p);
		} else {
			//click_chatter("Drop %d %d",state->_int_index,state->_int_left);

			drop = true;
			struct rte_mbuf* mbuf = get_mbuf(p,false);
			if (mbuf)
				rte_pktmbuf_free(mbuf);

			add_dropped(1);
		}
	} else {
		state->glob_pkts[state->_int_index + state->_int_left] = get_mbuf(p);
		state->_int_left++;
	}

	if (state->_int_left >= _burst || drop) {
		unsigned int ret = 0;
		unsigned int left = state->_int_left;
		unsigned int r;
		if (left) {
			lock();
			do {
				r = rte_eth_tx_burst(_port_no, queue_for_thread_begin(), &state->glob_pkts[state->_int_index + ret], left);
				left -= r;
				ret += r;

			} while (r == 32 && left > 0);
			unlock();
			add_count(ret);

			if (left == 0) {
				state->_int_left = 0;
				state->_int_index = 0;
			} else {
				state->_int_index = state->_int_index + ret;
				state->_int_left = left;
			}

		}
	}
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
void ToDpdkDevice::push_batch(int, PacketBatch *head)
{
	Packet* p = head;

	struct rte_mbuf **pkts = state->glob_pkts;

	if (state->_int_left) {
		//TODO : why never more than 32?
		unsigned ret = 0;
		unsigned r;
		unsigned left = state->_int_left;
		do {
			lock();
			r = rte_eth_tx_burst(_port_no, queue_for_thread_begin(), &state->glob_pkts[state->_int_index + ret] , left);
			unlock();
			ret += r;
			left -= r;
		} while (r == 32 && left > 0);

		if (ret == state->_int_left) {//all was sent
		    state->_int_left = 0;
			state->_int_index = 0;
			//Reset, there is nothing in the internal queue
		} else if (state->_int_index + state->_int_left + head->count() <  _internal_queue) {
			//Place the new packets after the old
		    state->_int_index += ret;
		    state->_int_left -= ret;
			pkts = &state->glob_pkts[state->_int_index + state->_int_left];
		} else if ((int)state->_int_index + (int)ret - (int)head->count() >= (int)0) {
			//Place the new packets before the older
		   // click_chatter("Before !");
			state->_int_index = (unsigned int)((int)state->_int_index - (int)head->count() + (int)ret);
			state->_int_left -= ret;
			pkts = &state->glob_pkts[state->_int_index];
		} else {
		    //Drop packets

			unsigned int lost = state->_int_left - ret;
			add_dropped(lost);
			//click_chatter("Dropped %d");
			for (int i = state->_int_index + ret; i < state->_int_index + state->_int_left; i++) {
				rte_pktmbuf_free(state->glob_pkts[i]);
			}

			state->_int_index = 0;
			state->_int_left = 0;
			//Reset, we will erase the old
		}

	}

	struct rte_mbuf **pkt = pkts;

	while (p != NULL) {
        *pkt = get_mbuf(p);
		pkt++;
		p = p->next();
	}

	unsigned ret = 0;
	unsigned r;
	unsigned left = head->count() + state->_int_left;
	do {
	    lock();
		r = rte_eth_tx_burst(_port_no, queue_for_thread_begin(), &state->glob_pkts[state->_int_index + ret] , left);
		unlock();
		ret += r;
		left -= r;
	} while (r == 32 && left > 0);
	add_count(ret);
	if (ret == head->count() + state->_int_left) { //All was sent
	    state->_int_index = 0;
	    state->_int_left = 0;
	} else {
	    state->_int_index = state->_int_index + ret;
	    state->_int_left = head->count() + state->_int_left - ret;
	}

#if !CLICK_DPDK_POOLS
    if (likely(is_fullpush()))
        head->safe_kill();
    else
        head->kill();
#endif

}
#endif

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel dpdk)
EXPORT_ELEMENT(ToDpdkDevice)
