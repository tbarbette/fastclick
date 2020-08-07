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

#include "todpdkdevicexchg.hh"

CLICK_DECLS

ToDPDKDeviceXCHG::ToDPDKDeviceXCHG()
{
}

ToDPDKDeviceXCHG::~ToDPDKDeviceXCHG()
{
}

int ToDPDKDeviceXCHG::configure(Vector<String> &conf, ErrorHandler *errh)
{
    int maxqueues = 128;
    String dev;
    if (Args(this, errh).bind(conf)
            .read_mp("PORT", dev)
            .consume() < 0)
        return -1;


    if (parse(conf, errh) != 0)
        return -1;

    if (Args(conf, this, errh)
        .read("TIMEOUT", _timeout)
        .read("NDESC",ndesc)
        .read("MAXQUEUES", maxqueues)
        .read("ALLOC",_create)
#if RTE_VERSION >= RTE_VERSION_NUM(18,02,0,0)
        .read("TSO", _tso)
        .read("IPCO", _ipco)
        .read("TCO", _tco)
#endif
        .complete() < 0)
            return -1;
    if (!DPDKDeviceArg::parse(dev, _dev)) {
        if (allow_nonexistent)
            return 0;
        else
            return errh->error("%s : Unknown or invalid PORT", dev.c_str());
    }

    //TODO : If user put multiple ToDPDKDeviceXCHG with the same port and without the QUEUE parameter, try to share the available queues among them
    if (firstqueue == -1)
       firstqueue = 0;
    if (n_queues == -1) {
        configure_tx(1,maxqueues,errh);
    } else {
        configure_tx(n_queues,n_queues,errh);
    }

    if (_tso)
        _dev->set_tx_offload(DEV_TX_OFFLOAD_TCP_TSO);
    if (_ipco)
        _dev->set_tx_offload(DEV_TX_OFFLOAD_IPV4_CKSUM);
    if (_tco) {
        _dev->set_tx_offload(DEV_TX_OFFLOAD_IPV4_CKSUM);
        _dev->set_tx_offload(DEV_TX_OFFLOAD_TCP_CKSUM);
    }
    return 0;
}

int ToDPDKDeviceXCHG::initialize(ErrorHandler *errh)
{
    int ret;

    ret = initialize_tx(errh);
    if (ret != 0)
        return ret;

    for (unsigned i = 0; i < (unsigned)n_queues; i++) {
        ret = _dev->add_tx_queue(i, ndesc , errh);
        if (ret != 0) return ret;    }

#if HAVE_BATCH
    if (batch_mode() == BATCH_MODE_YES) {
        if (_burst < 0)
            _burst = 1;
    } else
#endif
    {
        if (_burst < 0)
            _burst = 32;
    }

    if (ndesc > 0 && (unsigned)_burst > ndesc / 2 ) {
        errh->warning("BURST should not be upper than half the number of descriptor (%d)",ndesc);
    }

    if (_burst == 1 && _timeout == 0) {
        _timeout = -1;
    }

    ret = initialize_tasks(false,errh);
    if (ret != 0)
        return ret;

    for (unsigned i = 0; i < _iqueues.weight(); i++) {
        _iqueues.get_value(i).pkts = new struct rte_mbuf *[_internal_tx_queue_size];
        _iqueues.get_value(i).timeout.assign(this);
        _iqueues.get_value(i).timeout.initialize(this);
        _iqueues.get_value(i).timeout.move_thread(i);
    }

    _this_node = DPDKDevice::get_port_numa_node(_dev->port_id);

    //To set is_fullpush, we need to compute passing threads
    get_passing_threads();

    if (all_initialized()) {
        int ret = DPDKDevice::initialize(errh);
        if (ret != 0) return ret;
    }
    return 0;
}

inline void ToDPDKDeviceXCHG::set_flush_timer(DPDKDevice::TXInternalQueue &iqueue) {
    if (_timeout >= 0 || iqueue.nr_pending) {
        if (iqueue.timeout.scheduled()) {
            //No more pending packets, remove timer
            if (iqueue.nr_pending == 0)
                iqueue.timeout.unschedule();
        } else {
            if (iqueue.nr_pending > 0) {
                //Pending packets, set timeout to flush packets after a while even without burst
                if (_timeout <= 0)
                    iqueue.timeout.schedule_now();
                else
                    iqueue.timeout.schedule_after_msec(_timeout);
            }
        }
    }
}

void ToDPDKDeviceXCHG::run_timer(Timer *)
{
    flush_internal_tx_queue(_iqueues.get());
}

#ifdef DPDK_USE_XCHG
extern "C" {
#include <mlx5_xchg.h>
}
#endif

#if !defined(NOXCHG) && !defined(XCHG_TX_SWAPONLY)
    inline struct WritablePacket* get_tx_buf(struct xchg* x) {
        return (struct WritablePacket*)x;
    }

    uint16_t xchg_get_data_len(struct xchg* xchg) {
        return get_tx_buf(xchg)->length();
    }

    void xchg_tx_completed(struct rte_mbuf** elts, unsigned int part, unsigned int olx) {
        //mlx5_tx_free_mbuf(elts, part, olx);
    }

    bool xchg_do_tx_free = false;
    struct xchg* xchg_tx_next(struct xchg** xchgs) {
        WritablePacket** p = (WritablePacket**)xchgs;
        WritablePacket* pkt = *p;
        rte_prefetch0(pkt);
        return (struct xchg*)pkt;
    }

    int xchg_nb_segs(struct xchg* xchg) {
        //struct rte_mbuf* pkt = (struct rte_mbuf*) xchg;
        return 1; //NB_SEGS(pkt);
    }

    /* Advance in the list of packets, that is now permanently moved by one.
     * Returns the packet that was on top.*/
    void xchg_tx_advance(struct xchg*** xchgs_p) {
        struct WritablePacket** pkts = (WritablePacket**)(*xchgs_p);
        //printf("Advance : %p -> %p\n", *pkts, (*pkts)->next());
        *pkts = (WritablePacket*)(*pkts)->next();


    }

    void* xchg_get_buffer_addr(struct xchg* xchg) {
        //assert(DPDKDevice::is_dpdk_buffer(get_tx_buf(xchg)));
        //click_chatter("ADDR is %p", get_tx_buf(xchg)->buffer());
        return get_tx_buf(xchg)->buffer();
    }

    void* xchg_get_buffer(struct xchg* xchg) {
        return get_tx_buf(xchg)->data();
    }

    struct rte_mbuf* xchg_get_mbuf(struct xchg* xchg) {
        return (rte_mbuf*)((uint8_t*)xchg_get_buffer_addr(xchg) - sizeof(rte_mbuf));
    }

    void xchg_tx_sent_inline(struct xchg* xchg) {
        //printf("INLINED %p\n", xchg);
        //rte_pktmbuf_free_seg(pkt);
    }

    void xchg_tx_sent(struct rte_mbuf** elts, struct xchg** xchgs) {
        //printf("SENT %p\n", *xchg);
        struct rte_mbuf* tmp = *elts;
        if (tmp == 0)
            tmp = DPDKDevice::get_pkt();
        //assert(tmp);

        //struct rte_mbuf* mbuf = DPDKDevice::get_mbuf((WritablePacket*)*xchgs, false, -1, false);
        //assert(mbuf);
        struct rte_mbuf* mbuf = xchg_get_mbuf(*xchgs);
        *elts = mbuf;
        get_tx_buf(*xchgs)->set_buffer(((uint8_t*)tmp) + sizeof(rte_mbuf), DPDKDevice::MBUF_DATA_SIZE);



    }

    bool xchg_elts_vec = false;

    void xchg_tx_sent_vec(struct rte_mbuf** elts, struct xchg** xchg, unsigned n) {
    }
#elif !defined(NOXCHG)

/**
 * Free the mbufs from the linear array of pointers.
 *
 * @param pkts
 *   Pointer to array of packets to be free.
 * @param pkts_n
 *   Number of packets to be freed.
 * @param olx
 *   Configured Tx offloads mask. It is fully defined at
 *   compile time and may be used for optimization.
 */
static __rte_always_inline void
mlx5_tx_free_mbuf(struct rte_mbuf ** pkts,
		  unsigned int pkts_n,
		  unsigned int olx __rte_unused)
{
	struct rte_mempool *pool = NULL;
	struct rte_mbuf **p_free = NULL;
	struct rte_mbuf *mbuf;
	unsigned int n_free = 0;

	/*
	 * The implemented algorithm eliminates
	 * copying pointers to temporary array
	 * for rte_mempool_put_bulk() calls.
	 */
	for (;;) {
		for (;;) {
			/*
			 * Decrement mbuf reference counter, detach
			 * indirect and external buffers if needed.
			 */
			mbuf = rte_pktmbuf_prefree_seg(*pkts);
			if (likely(mbuf != NULL)) {
				if (likely(n_free != 0)) {
					if (unlikely(pool != mbuf->pool))
						/* From different pool. */
						break;
				} else {
					/* Start new scan array. */
					pool = mbuf->pool;
					p_free = pkts;
				}
				++n_free;
				++pkts;
				--pkts_n;
				if (unlikely(pkts_n == 0)) {
					mbuf = NULL;
					break;
				}
			} else {
				/*
				 * This happens if mbuf is still referenced.
				 * We can't put it back to the pool, skip.
				 */
				++pkts;
				--pkts_n;
				if (unlikely(n_free != 0))
					/* There is some array to free.*/
					break;
				if (unlikely(pkts_n == 0))
					/* Last mbuf, nothing to free. */
					return;
			}
		}
		for (;;) {
			/*
			 * This loop is implemented to avoid multiple
			 * inlining of rte_mempool_put_bulk().
			 */
			/*
			 * Free the array of pre-freed mbufs
			 * belonging to the same memory pool.
			 */
			rte_mempool_put_bulk(pool, ( void * const*)p_free, n_free);
			if (unlikely(mbuf != NULL)) {
				/* There is the request to start new scan. */
				pool = mbuf->pool;
				p_free = pkts++;
				n_free = 1;
				--pkts_n;
				if (likely(pkts_n != 0))
					break;
				/*
				 * This is the last mbuf to be freed.
				 * Do one more loop iteration to complete.
				 * This is rare case of the last unique mbuf.
				 */
				mbuf = NULL;
				continue;
			}
			if (likely(pkts_n == 0))
				return;
			n_free = 0;
			break;
		}
	}
}
/*
 * This is just an intermediate testing mode, where we keep mbuf but we do the
 * exchange of buffers to avoid going through the pool.
 */
    inline struct rte_mbuf* get_tx_buf(struct xchg* x) {
        return (struct rte_mbuf*)x;
    }

    uint16_t xchg_get_data_len(struct xchg* xchg) {
        return rte_pktmbuf_data_len(get_tx_buf(xchg));
    }

    void xchg_tx_completed(struct rte_mbuf** elts, unsigned int part, unsigned int olx) {
        //mlx5_tx_free_mbuf(elts, part, olx);
    }

    bool xchg_do_tx_free = false;

    struct xchg* xchg_tx_next(struct xchg** xchgs) {
        struct rte_mbuf** pkts = (struct rte_mbuf**)xchgs;
        struct rte_mbuf* pkt = *(pkts);
        rte_prefetch0(pkt);
        return (struct xchg*)pkt;
    }

    int xchg_nb_segs(struct xchg* xchg) {
        struct rte_mbuf* pkt = (struct rte_mbuf*) xchg;
        return 1; //NB_SEGS(pkt);
    }

    void xchg_tx_advance(struct xchg*** xchgs_p) {
        struct rte_mbuf** pkts = (struct rte_mbuf**)(*xchgs_p);
//        printf("Advance : %p -> %p = %p\n", pkts, pkts+1, *(pkts+1));
//        assert(*pkts == (struct rte_mbuf*)0x87);
        pkts += 1;
        *xchgs_p = (struct xchg**)pkts;
       // struct rte_mbuf* pkt = *(pkts);
    }

    void* xchg_get_buffer_addr(struct xchg* xchg) {
        struct rte_mbuf* pkt = (struct rte_mbuf*) xchg;
        return pkt->buf_addr;
    }

    void* xchg_get_buffer(struct xchg* xchg) {
        struct rte_mbuf* pkt = (struct rte_mbuf*) xchg;
        return rte_pktmbuf_mtod(pkt, void *);
    }

    struct rte_mbuf* xchg_get_mbuf(struct xchg* xchg) {
        return get_tx_buf(xchg);
    }

    void xchg_tx_sent_inline(struct xchg* xchg) {
        struct rte_mbuf* pkt = (struct rte_mbuf*) xchg;
        
        //printf("INLINED %p\n", xchg);
        //rte_pktmbuf_free_seg(pkt);
    }

    void xchg_tx_sent(struct rte_mbuf** elts, struct xchg** xchg) {
        //printf("SENT %p\n", *xchg);
        struct rte_mbuf* tmp = *elts;
        *elts = (struct rte_mbuf*)*xchg;
        *xchg = (struct xchg*)tmp;

    }

    bool xchg_elts_vec = false;

    void xchg_tx_sent_vec(struct rte_mbuf** elts, struct xchg** xchg, unsigned n) {
    }
#endif


/* Flush as much as possible packets from a given internal queue to the DPDK
 * device. */
void ToDPDKDeviceXCHG::flush_internal_tx_queue(DPDKDevice::TXInternalQueue &iqueue) {
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

    lock(); // ! This is a queue lock, not a thread lock.

    do {
        sub_burst = iqueue.nr_pending > 32 ? 32 : iqueue.nr_pending;
        if (iqueue.index + sub_burst >= (unsigned)_internal_tx_queue_size)
            // The sub_burst wraps around the ring
            sub_burst = _internal_tx_queue_size - iqueue.index;
        //Todo : if there is multiple queue assigned to this thread, send on all of them


#ifdef DPDK_USE_XCHG
        r = rte_mlx5_tx_burst_xchg(_dev->port_id, queue_for_thisthread_begin(),(struct xchg**) &iqueue.pkts[iqueue.index], sub_burst);
#else
        assert(false);
#endif

        iqueue.nr_pending -= r;
        iqueue.index += r;

        if (iqueue.index >= (unsigned)_internal_tx_queue_size) // Wrapping around the ring
            iqueue.index = 0;

        sent += r;
    } while (r == sub_burst && iqueue.nr_pending > 0);
    unlock();

    add_count(sent);
}

void ToDPDKDeviceXCHG::push(int, Packet *p)
{
    // Get the thread-local internal queue
    DPDKDevice::TXInternalQueue &iqueue = _iqueues.get();

    bool congestioned;
    do {
        congestioned = false;

        if (iqueue.nr_pending == (unsigned)_internal_tx_queue_size) { // Internal queue is full
            /* We just set the congestion flag. If we're in blocking mode,
             * we'll loop, else we'll drop this packet.*/
            congestioned = true;
            if (!_blocking) {
                add_dropped(1);
                if (!_congestion_warning_printed) {
                    click_chatter("%s: packet dropped", name().c_str());
                    _congestion_warning_printed = true;
                }
            } else if (!_congestion_warning_printed) {
                click_chatter("%s: congestion warning", name().c_str());
                _congestion_warning_printed = true;
            }
        } else { // If there is space in the iqueue
            struct rte_mbuf* mbuf = DPDKDevice::get_mbuf(p, _create, _this_node);
            if (mbuf != NULL) {
                enqueue(iqueue.pkts[(iqueue.index + iqueue.nr_pending) % _internal_tx_queue_size], mbuf, (WritablePacket*)p);
                iqueue.nr_pending++;
            } else {
                click_chatter("No more DPDK buffer");
                abort();
            }
        }

        if ((int)iqueue.nr_pending >= _burst || congestioned) {
            flush_internal_tx_queue(iqueue);
        }
        set_flush_timer(iqueue); //We wait until burst for sending packets, so flushing timer is especially important here

        // If we're in blocking mode, we loop until we can put p in the iqueue
    } while (unlikely(_blocking && congestioned));

#if !CLICK_PACKET_USE_DPDK
    if (likely(is_fullpush()))
        p->kill_nonatomic();
    else
        p->kill();
#endif
}


inline void
ToDPDKDeviceXCHG::enqueue(rte_mbuf* &q, rte_mbuf* mbuf, WritablePacket* p) {
    if (q == 0) 
        q = DPDKDevice::get_pkt(_this_node);
    p->set_buffer((unsigned char*)q + sizeof(rte_mbuf), DPDKDevice::MBUF_DATA_SIZE);
    p->set_buffer_destructor(DPDKDevice::free_pkt);
    q = mbuf;
}

/**
 * push_batch seems more complex than in tonetmapdevice, but it's only because
 *  we have to place pointers in an array, and we don't want to keep a linked
 *  list plus an array (we could end up with packets which were not sent in the
 *  array, and packets in the list, it would be a mess). So we use an array as
 *  a ring.
 */
#if HAVE_BATCH
void ToDPDKDeviceXCHG::push_batch(int, PacketBatch *head)
{
    // Get the thread-local internal queue
#if TD_HAS_IQUEUE
    DPDKDevice::TXInternalQueue &iqueue = _iqueues.get();
#endif


#if TD_HAS_IQUEUE
#if !defined(XCHG_TX_SWAPONLY)
    assert(false);
#endif
    Packet* p = head;
    Packet* next;
    do {
        congestioned = false;


        //First, place the packets in the queue
        while (iqueue.nr_pending < (unsigned)_internal_tx_queue_size && p) { // Internal queue is full
            // While there is still place in the iqueue
            struct rte_mbuf* mbuf = DPDKDevice::get_mbuf(p, _create, _this_node);
            if (mbuf != NULL) {
                enqueue(iqueue.pkts[(iqueue.index + iqueue.nr_pending) & (_internal_tx_queue_size - 1)], mbuf, (WritablePacket*)p);
                iqueue.nr_pending++;
            } else {
                click_chatter("No more DPDK buffer");
                abort();
            }
            next = p->next();
            p = next;
        }

        if (unlikely(p) != 0) {
            congestioned = true;
            warn_congestion();
        }

        //Flush the queue if we have pending packets
        if ((int)iqueue.nr_pending >= _burst || congestioned) {
            flush_internal_tx_queue(iqueue);
        }
        set_flush_timer(iqueue);
        // If we're in blocking mode, we loop until we can put p in the iqueue
    } while (unlikely(_blocking && congestioned));

# if !CLICK_PACKET_USE_DPDK
    //If non-blocking, drop all packets that could not be sent
    while (p) {
        next = p->next();
        BATCH_RECYCLE_PACKET_CONTEXT(p);
        p = next;
        add_dropped(1);
    }
# endif
# if !CLICK_PACKET_USE_DPDK
    BATCH_RECYCLE_END();
# endif

#else //No iqueue
    
    //The batch will always get a new buffer (or sent inline), so we can
    // recycle the whole batch in the end as a packet-data batch
    // none of the annotations will be changed (next, ...) so it's safe
    // to keep the count, tail, etc
# if defined(XCHG_TX_SWAPONLY)
    Packet* p = head->first();
    int count = head->count();

    rte_mbuf* pkts_s[count] = {0};
    for (int i = 0; i < count; i++) {
        struct rte_mbuf* mbuf = DPDKDevice::get_mbuf(p, _create, _this_node, false);
        pkts_s[i] = mbuf;
        //click_chatter("Queue [%d] -> %p", i, mbuf);
        p = p->next();
    }
    p = head;
    rte_mbuf** pkts = pkts_s;
send:

#  ifdef DPDK_USE_XCHG
    unsigned r = rte_mlx5_tx_burst_xchg(_dev->port_id, queue_for_thisthread_begin(),(struct xchg**) pkts, count);
#  else
    unsigned r = 0;
    assert(false);
#  endif
    //click_chatter("SENT %d/%d", r, count);
    for (int i = 0; i < r; i++) {
        if (pkts[i] == 0) 
            pkts[i] = DPDKDevice::get_pkt(_this_node); 
        ((WritablePacket*)p)->set_buffer((unsigned char*)pkts[i] + sizeof(rte_mbuf), DPDKDevice::MBUF_DATA_SIZE);
        p = p->next();
        //click_chatter("Queue [%d] -> %p", i, pkts[i]);
    }
    if (unlikely(r != count)) {
        warn_congestion();
        if (_blocking) {
            count -= r;
            pkts += r;
            goto send;
        } else {
            //Nothing to do : packets are still in the batch, others have been swapped!
        }
    }
# else //No SWAPONLY
    unsigned count = head->count();
    Packet* sent = head->first();
send:
    //click_chatter("SEND %d, %p, buffer %p", count, sent, sent->buffer());
#ifdef DPDK_USE_XCHG
    unsigned r = rte_mlx5_tx_burst_xchg(_dev->port_id, queue_for_thisthread_begin(),(struct xchg**)&sent, count);
#else
    unsigned r = 0;
    assert(false);
#endif
    //click_chatter("SENT %d, %p", r, sent);
    if (unlikely(r != count)) {
        warn_congestion();
        if (_blocking) {
            count -= r;
            goto send;
        } else {
            //Nothing to do : packets are still in the batch, others have been swapped!
        }

    }
# endif
   head->recycle_batch(true);
#endif

}
#endif
CLICK_ENDDECLS
ELEMENT_REQUIRES(ToDPDKDevice userlevel dpdk !dpdk-packet)
EXPORT_ELEMENT(ToDPDKDeviceXCHG)
ELEMENT_MT_SAFE(ToDPDKDeviceXCHG)
