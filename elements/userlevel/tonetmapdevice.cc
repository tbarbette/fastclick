// -*- c-basic-offset: 4; related-file-name: "tonetmapdevice.hh" -*-
/*
 * tonetmapdevice.{cc,hh} -- element reads packets live from network via
 * Netamap
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
#include "tonetmapdevice.hh"
#include <click/args.hh>
#include <click/error.hh>
#include <click/standard/scheduleinfo.hh>
#include <click/packet_anno.hh>
#include <vector>
#include <click/master.hh>
#include <sstream>

CLICK_DECLS

ToNetmapDevice::ToNetmapDevice() : _device(0),_burst(32),_block(1),_internal_queue(512)
{

}


int
ToNetmapDevice::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String ifname;
    int maxthreads = -1;
    int burst = -1;

    if (Args(conf, this, errh)
    .read_mp("DEVNAME", ifname)
    .read_p("IQUEUE",_internal_queue)
    .read_p("BLOCKANT",_block)
    .read("BURST", burst)
    .read("MAXTHREADS",maxthreads)
    .complete() < 0)
    	return -1;

    if (_internal_queue < _burst * 2) {
        return errh->error("IQUEUE (%d) must be at least twice the size of BURST (%d)!",_internal_queue, _burst);
    }

#if HAVE_BATCH
    if (burst > 0) {
        if (input_is_pull(0))
            errh->warning("%s: burst does not make sense in full push with batching, it is unused.",name().c_str());
        _burst = burst;
    }
#else
    if (burst > 0)
        _burst = burst;
#endif
    _device = NetmapDevice::open(ifname);
    if (!_device) {
        return errh->error("Could not initialize %s",ifname.c_str());
    }
    configure_tx(maxthreads,1,_device->n_queues,errh); //Using the fewer possible number of queues is the better

    if (_burst > _device->some_nmd->some_ring->num_slots / 2) {
        errh->warning("BURST value larger than half the ring size (%d) is not recommended. Please set BURST to %d or less",_burst, _device->some_nmd->some_ring->num_slots,_device->some_nmd->some_ring->num_slots/2);
    }

    return 0;
}

int ToNetmapDevice::initialize(ErrorHandler *errh)
{
    int ret;

    ret = initialize_tx(errh);
    if (ret != 0)
        return ret;

    ret = initialize_tasks(false,errh);
    if (ret != 0)
        return ret;

	/*Do not use select mechanism in push mode, we'll use the rings to absorb
		transient traffic, the iqueue if it's not enough, and block or drop if
		even the iqueue is full*/
	if (input_is_pull(0)) {
		_queues.resize(router()->master()->nthreads());
		for (int i = 0; i < nqueues; i++) {
			master()->thread(thread_for_queue(i))->select_set().add_select(_device->nmds[i]->fd,this,SELECT_WRITE);
		}

		int nt = 0;
		for (int i = 0; i < click_nthreads; i++) {
			if (!usable_threads[i]) continue;
			_queues[i] = 0;
			state.get_value_for_thread(i).signal = (Notifier::upstream_empty_signal(this, 0, _tasks[nt]));
			state.get_value_for_thread(i).timer = new Timer(task_for_thread(i));
			state.get_value_for_thread(i).timer->initialize(this);
			state.get_value_for_thread(i).backoff = 1;
			nt++;
		}
	} else {
		_iodone.resize(nqueues);
		_zctimers.resize(nqueues);
		for (int i = 0; i < nqueues; i++) {
		    _iodone[i] = false;
		    _zctimers[i] = new Timer(this);
		    _zctimers[i]->initialize(this,false);
		    _zctimers[i]->move_thread(thread_for_queue(i));
	     }
	}

	//Wait for Netmap init if we're the last interface
    if (all_initialized()) {
        click_chatter("Waiting 3 sec for full hardware initialization of netmap devices...\n");
        sleep(3);
    }

	return 0;
}

void
ToNetmapDevice::selected(int fd, int)
{
	task_for_thread()->reschedule();
	master()->thread(click_current_cpu_id())->select_set().remove_select(fd,this,SELECT_WRITE);
}

inline void ToNetmapDevice::allow_txsync() {
    for (int i = queue_for_thread_begin(); i <= queue_for_thread_end(); i++)
        _iodone[i] = false;
}

/**
 * Do a full synchronization of a transmit ring. It will both update the NIC
 *  ring with previously added packets, and ask the NIC which packets
 *  are sent and can be freed.
 */
inline void ToNetmapDevice::do_txsync(int fd) {
    ioctl(fd,NIOCTXSYNC,0);
}

/**
 * Will synchronize the transmit ring in a lighter mode. It will update the NIC
 *  ring with previously added packets but will only reclaim space from
 *  sent packets if an interrupt has been sent to the TX ring.
 * This require our modified netmap version, but will still work with vanilla
 *  Netmap.
 */
inline void ToNetmapDevice::do_txreclaim(int fd) {
    ioctl(fd,NIOCTXSYNC,1);
}

/**
 * Will do a full ring synchronization if the IODONE flag is down
 */
inline void ToNetmapDevice::try_txsync(int queue, int fd) {
#if !HAVE_BATCH
    if (!_iodone[queue]) {
#endif
        do_txsync(fd);
#if !HAVE_BATCH
        _iodone[queue] = true;
    }
#endif
}

#if HAVE_BATCH
void
ToNetmapDevice::push_batch(int port, PacketBatch *b_head)
{
	State &s = state.get();
   	bool should_be_dropped = false;
   	bool ask_sync = false;
#if HAVE_FLOW
    FlowControlBlock* sfcb_save = fcb_stack;
       if (fcb_stack)
           fcb_stack->release(b_head->count());
   	fcb_stack = 0;
#endif
	if (s.q != NULL) {
		if (s.q_size < _internal_queue) {
			s.q->prev()->set_next(b_head);
			s.q_size += b_head->count();
			s.q->set_prev(b_head->prev());
		} else {
		    //We don't have space to store these packets, but we'll try to send the awaiting packets before dropping them
		    should_be_dropped = true;
		}

	} else {
		s.q = b_head;
		s.q_size = b_head->count();

		//If we are not struggling, ask a sync at the end of the batch
		ask_sync = true;
	}

do_send_batch:
	Packet* last = s.q->prev();

    lock(); //Lock only if multiple threads can go here
    unsigned sent = send_packets(s.q,true,ask_sync);
    unlock();

    if (sent > 0 && s.q)
    	s.q->set_prev(last);

    s.q_size -= sent;

    if (s.q == NULL) { //All was placed in the ring
        if (should_be_dropped) {
            //we can give a second try to the awaiting packets
            push_batch(port, b_head);
        }
    } else {
        if (should_be_dropped) {
            if (s.q_size < _internal_queue) {
                s.q->prev()->set_next(b_head);
                s.q_size += b_head->count();
                s.q->set_prev(b_head->prev());
            } else { //Still not enough place, drop !
            	if (_block) {
            		allow_txsync();
            		goto do_send_batch;
            	} else {
                    add_dropped(b_head->count());
                    b_head->kill();
            	}
            }
        }
    }
#if HAVE_FLOW
   	fcb_stack = sfcb_save;
#endif
}
#endif
void
ToNetmapDevice::push(int, Packet* p) {
	State &s = state.get();
	if (s.q == NULL) {
		s.q = p;
		p->set_prev(p);
		p->set_next(NULL); //Just to be sure, even if it should already be
		s.q_size = 1;
	} else {
		if (s.q_size < _internal_queue) { //Append packet at the end
			#if HAVE_FLOW
			if (fcb_stack)
				fcb_stack->release(1);
			#endif
			s.q->prev()->set_next(p);
			s.q->set_prev(p);
			s.q_size++;
		} else {
			add_dropped(1);
			p->kill();
		}
	}

	if (s.q_size >= _burst) { //TODO "or if timeout", not implemented yet because batching solves this problem
do_send:
		Packet* last = s.q->prev();

		/*As we arrive here once every packet, we just try to take the lock,
			if we can't grab it, we'll simply re-try at the next packet
			this should also set a timer to cope with very low throughput,
			but again batching solves this problem*/
		if (lock_attempt()) {
			s.q->prev()->set_next(NULL);
		} else if (unlikely(s.q_size > 2*_burst || (_block && s.q_size >= _internal_queue))) {
			//If it failed too much... We'll spinlock, or if we are in blockant mode and we need to block
			s.q->prev()->set_next(NULL);
			lock();
		} else { //"Failed lock but not too much packets are waiting
			return;
		}

		//Lock is acquired
		unsigned int sent;
		SFCB_STACK(sent = send_packets(s.q,true););

		if (sent > 0 && s.q)
			s.q->set_prev(last);

		s.q_size -= sent;

		if (s.q && s.backoff < 128) {
			s.backoff++;

			if (!_zctimers[queue_for_thread_begin()]->scheduled()) {
				_zctimers[queue_for_thread_begin()]->schedule_after(Timestamp::make_usec(1));
			}
		} else {
			//If we backed off a lot, we may try to do a sync before waiting for the timer to trigger
			//or if we could send everything we remove the backoff and allow sync too
			s.backoff = 0;
			allow_txsync();
		}
		unlock();

		if (s.q && s.q_size >= _internal_queue && _block) {
			allow_txsync();
			goto do_send;
		}
	}
}

/*Timer for push mode. It will raise the IODONE flag when running to allow a new
 * full synchronization of the ring to be done.*/
void
ToNetmapDevice::run_timer(Timer *) {
    allow_txsync();
}

int complaint = 0;

/**
 * Send a linked list of packet, return the number of packet sent and the head
 * 	points toward the packets following the last sent packet (could be null)
 *
 * @return The number of packets sent
 */
inline unsigned int ToNetmapDevice::send_packets(Packet* &head, bool push, bool ask_sync) {
	State &s = state.get();
	struct nm_desc* nmd;
	struct netmap_ring *txring;
	struct netmap_slot *slot;

	WritablePacket* s_head = head->uniqueify();
	WritablePacket* next = s_head;
	WritablePacket* p = next;

	unsigned int sent = 0;

	for (int iloop = 0; iloop < queue_per_threads; iloop++) {
		int in = (s.last_queue + iloop) % queue_per_threads;
		int i =  queue_for_thread_begin() + in;

		nmd = _device->nmds[i];
		txring = NETMAP_TXRING(nmd->nifp, i);

		if (nm_ring_empty(txring)) {
			if (push)
				try_txsync(i,nmd->fd);
			continue;
		}

		u_int cur = txring->cur;
#if HAVE_BATCH
		WritablePacket* last = NULL; //Remember the last treated packet (p = last->next())
#endif
		while ((cur != txring->tail) && next) {
			p = next;

			next = static_cast<WritablePacket*>(p->next());

			slot = &txring->slot[cur];
			slot->len = p->length();
#if HAVE_NETMAP_PACKET_POOL
			if (p->headroom() > 0) {
				complaint++;
				if (complaint < 5)
					click_chatter("Shifting data in %s. You should avoid this case !");
				p = static_cast<PacketBatch*>(p->shift_data(-p->headroom())); //Is it better to shift or to copy like if it was not a netmap buffer?
			}

			unsigned char * tx_buffer_data = (unsigned char*)NETMAP_BUF(txring,slot->buf_idx);
			slot->buf_idx = NETMAP_BUF_IDX(txring,p->buffer());
			static_cast<WritablePacket*>(p)->set_buffer(tx_buffer_data,txring->nr_buf_size);
			slot->flags |= NS_BUF_CHANGED;

			if (unlikely(push && cur % 32 == 0)) {
				ask_sync = true;
				slot->flags |= NS_REPORT;
			}
#else
# if HAVE_ZEROCOPY
			if (likely(NetmapBufQ::is_netmap_packet(p))) {
				((NetmapBufQ*)(p->destructor_argument()))->insert(slot->buf_idx);
				slot->buf_idx = NETMAP_BUF_IDX(txring,p->buffer());
				slot->flags |= NS_BUF_CHANGED;
				p->set_buffer_destructor(NetmapBufQ::buffer_destructor_fake);
			} else
# endif
			{
				unsigned char* dstdata = (unsigned char*)NETMAP_BUF(txring, slot->buf_idx);
				void* srcdata = (void*)(p->data());
				memcpy(dstdata,srcdata,p->length());
			}
#endif
#if HAVE_BATCH && HAVE_CLICK_PACKET_POOL
			//We cannot recycle a batch with shared packet in it
			if (p->shared()) {
				p->kill();
				if (last)
					last->set_next(next);
			} else {
				last = p;
			}
#else
			//If no batch or no packet pool, don't bother recycling per-batch
			p->kill();
#endif
			sent++;
			cur = nm_ring_next(txring,cur);
		}
		txring->head = txring->cur = cur;

		if (unlikely(ask_sync))
			do_txsync(nmd->fd);

		if (next == NULL) { //All is sent
			add_count(sent);
#if HAVE_BATCH && HAVE_CLICK_PACKET_POOL
			p->set_next(0);
			PacketBatch::make_from_list(s_head,sent)->safe_kill();
#endif
			head = NULL;
			return sent;
		}
	}

	if (next == s_head) { //Nothing could be sent...
		head = s_head;
		return 0;
	} else {
#if HAVE_BATCH && HAVE_CLICK_PACKET_POOL
		p->set_next(0);
		PacketBatch::make_from_simple_list(s_head,p,sent)->safe_kill();
#endif
		add_count(sent);
		head = next;
		return sent;
	}
}

bool
ToNetmapDevice::run_task(Task* task)
{
	State &s = state.get();

	unsigned int total_sent = 0;

	Packet* batch = s.q;
	unsigned batch_size = s.q_size;
	s.q = NULL;
	s.q_size = 0;
	do {
#if HAVE_BATCH
		if (!batch || batch_size < _burst) {
			PacketBatch* new_batch = input(0).pull_batch(_burst - batch_size);
			if (new_batch) {
				if (batch) {
					batch->prev()->set_next(new_batch);
					batch_size += new_batch->count();
					batch->set_prev(new_batch->tail());
				} else {
					batch = new_batch;
					batch_size = new_batch->count();
				}
			}
		}
#else
		/* Difference from vanilla is that we build a batch up to _burst size
		 * and then process it. This allows to synchronize less often, which is
		 * costly with netmap. */
		if (!batch || batch_size < _burst) { //Create a batch up to _burst size, or less if no packets are available
			Packet* last;
			if (!batch) {
				//Nothing in the internal queue
				if ((batch = input(0).pull()) == NULL) {
					//Nothing to pull
					break;
				}
				last = batch;
				batch_size = 1;
			} else {
				last = batch->prev();
			}

			Packet* p;
			//Pull packets until we reach burst or there is nothing to pull
			while (batch_size < _burst && (p = input(0).pull()) != NULL) {
				last->set_next(p);
				last = p;
				batch_size++;
			}
			batch->set_prev(last); //Prev of the head is the tail of the batch
		}
#endif

		if (batch) {
			Packet* last = batch->prev();
			last->set_next(NULL); //Just to be sure

			lock();
			unsigned int sent = send_packets(batch,false);
			unlock();

			total_sent += sent;
			batch_size -= sent;

			if (sent > 0) { //At least one packet sent
				if (batch) //Reestablish the tail if we could not send everything
					batch->set_prev(last);
			} else
				break;
		} else //No packet to send
			break;
	} while (1);

	if (batch != NULL) {/*Output ring is full, we rely on the select mechanism
		to know when we'll have space to send packets*/
		s.backoff = 1;
		s.q = batch;
		s.q_size = batch_size;
		//Register fd to wait for space
		for (int i = queue_for_thread_begin(); i <= queue_for_thread_end(); i++) {
			master()->thread(click_current_cpu_id())->select_set().add_select(_device->nmds[i]->fd,this,SELECT_WRITE);
		}
	} else if (s.signal.active()) { //TODO is this really needed?
		//We sent everything we could, but check signal to see if packet arrived after last read
		task->fast_reschedule();
	} else { //Empty and no signal
		if (s.backoff < 256)
			s.backoff*=2;
		s.timer->schedule_after(Timestamp::make_usec(s.backoff));

	}
	return total_sent;
}


void
ToNetmapDevice::cleanup(CleanupStage)
{
    cleanup_tasks();
    for (unsigned int i = 0; i < state.size(); i++) {
        if (state.get_value(i).q) {
        	Packet* next = state.get_value(i).q->next();
        	state.get_value(i).q->kill();
        	state.get_value(i).q = next;
        }
        if (state.get_value(i).timer)
            delete state.get_value(i).timer;
    }
    if (!input_is_pull(0))
        for (int i = 0; i < min((int)_zctimers.size(),(int)nqueues); i++)
            delete _zctimers[i];
    if (_device) _device->destroy();
}


void
ToNetmapDevice::add_handlers()
{
    add_read_handler("n_sent", count_handler, 0);
    add_read_handler("n_dropped", dropped_handler, 0);
    add_write_handler("reset_counts", reset_count_handler, 0, Handler::BUTTON);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel netmap QueueDevice)
EXPORT_ELEMENT(ToNetmapDevice)
