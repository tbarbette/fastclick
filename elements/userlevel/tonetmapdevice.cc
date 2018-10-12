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

ToNetmapDevice::ToNetmapDevice() : _pull_use_select(true),_device(0)
{
	_burst = 32;
	_blocking = true;
	_internal_tx_queue_size = 512;
}


int
ToNetmapDevice::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String ifname;
    int burst = -1;

    if (Args(this, errh).bind(conf)
            .read_mp("DEVNAME", ifname)
            .complete() < 0)
    	return -1;

    if (_internal_tx_queue_size < _burst * 2) {
        return errh->error("IQUEUE (%d) must be at least twice the size of BURST (%d)!",_internal_tx_queue_size, _burst);
    }

#if HAVE_BATCH
    if (burst > 0) {
        if (input_is_push(0) && in_batch_mode == BATCH_MODE_YES && _verbose)
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

    //TODO : If user put multiple ToNetmapDevice with the same port and without the QUEUE parameter, try to share the available queues among them
    if (firstqueue == -1)
        firstqueue = 0;
    if (firstqueue >= _device->n_queues)
        return errh->error("You asked for queue %d but device only have %d queues.",firstqueue,_device->n_queues);

    configure_tx(1,_device->n_queues,errh); //Using the fewer possible number of queues is the better

    if (_burst > _device->get_num_slots() / 2) {
        errh->warning("BURST value larger than half the ring size (%d) is not recommended. Please set BURST to %d or less",_burst, _device->some_nmd->some_ring->num_slots,_device->some_nmd->some_ring->num_slots/2);
    }

#if HAVE_BATCH
    if (ninputs() && input_is_pull(0))
        in_batch_mode = BATCH_MODE_YES;
#endif

    return 0;
}

int ToNetmapDevice::initialize(ErrorHandler *errh)
{
    int ret;

    ret = initialize_tx(errh);
    if (ret != 0)
        return ret;

    ret = initialize_tasks(input_is_pull(0) && !_pull_use_select,errh);
    if (ret != 0)
        return ret;

	/*Do not use select mechanism in push mode, we'll use the rings to absorb
		transient traffic, the iqueue if it's not enough, and block or drop if
		even the iqueue is full*/
	if (input_is_pull(0)) {
		if (_pull_use_select) {
			for (int i = 0; i < n_queues; i++) {
				master()->thread(thread_for_queue(i))->select_set().add_select(_device->nmds[i]->fd,this,SELECT_WRITE);
			}
		}

		int nt = 0;
		for (int i = 0; i < click_max_cpu_ids(); i++) {
			if (!usable_threads[i]) continue;
			state.get_value_for_thread(i).signal = (Notifier::upstream_empty_signal(this, 0, _tasks[nt]));
			state.get_value_for_thread(i).timer = new Timer(task_for_thread(i));
			state.get_value_for_thread(i).timer->initialize(this);
			state.get_value_for_thread(i).backoff = 1;
			nt++;
		}
	} else {
		_iodone.resize(firstqueue + n_queues);
		_zctimers.resize(firstqueue + n_queues);
		for (int i = firstqueue; i < firstqueue + n_queues; i++) {
		    _iodone[i] = false;
		    _zctimers[i] = new Timer(this);
		    _zctimers[i]->initialize(this,false);
		    _zctimers[i]->move_thread(thread_for_queue(i));
	     }
	}

	return 0;
}

inline void ToNetmapDevice::allow_txsync() {
    for (int i = queue_for_thisthread_begin(); i <= queue_for_thisthread_end(); i++)
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
 *
 *  Only supported with modified netmap !
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

	if (s.q != NULL) {
		if (s.q_size < _internal_tx_queue_size) {
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
    unsigned sent = send_packets(s.q,ask_sync,true);
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
            if (s.q_size < _internal_tx_queue_size) {
                s.q->prev()->set_next(b_head);
                s.q_size += b_head->count();
                s.q->set_prev(b_head->prev());
            } else { //Still not enough place, drop !
		if (_blocking) {
            		allow_txsync();
            		goto do_send_batch;
            	} else {
                    add_dropped(b_head->count());
                    b_head->kill();
            	}
            }
        }
    }
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
		if (s.q_size < _internal_tx_queue_size) { //Append packet at the end
			s.q->prev()->set_next(p);
			s.q->set_prev(p);
			s.q_size++;
		} else {
			//TODO : blocking mode
			add_dropped(1);
			p->kill();
		}
	}

	if (s.q_size >= _burst) { //TODO "or if timeout", not implemented yet because batching +- solves this problem
do_send:
		Packet* last = s.q->prev();

		/*As we arrive here once every packet, we just try to take the lock,
			if we can't grab it, we'll simply re-try at the next packet
			this should also set a timer to cope with very low throughput,
			but again batching solves this problem*/
		if (lock_attempt()) {
			s.q->prev()->set_next(NULL);
		} else if (unlikely(s.q_size > 2*_burst || (_blocking && s.q_size >= _internal_tx_queue_size))) {
			//If it failed too much... We'll spinlock, or if we are in blockant mode and we need to block
			s.q->prev()->set_next(NULL);
			lock();
		} else { //"Failed lock but not too much packets are waiting
			return;
		}

		//Lock is acquired
		unsigned int sent = send_packets(s.q,false,false);

		if (sent > 0 && s.q)
			s.q->set_prev(last);

		s.q_size -= sent;

		if (s.q && s.backoff < 128) {
			s.backoff++;

			if (!_zctimers[queue_for_thisthread_begin()]->scheduled()) {
				_zctimers[queue_for_thisthread_begin()]->schedule_after(Timestamp::make_usec(1));
			}
		} else {
			//If we backed off a lot, we may try to do a sync before waiting for the timer to trigger
			//or if we could send everything we remove the backoff and allow sync too
			s.backoff = 0;
			allow_txsync();
		}
		unlock();

		if (s.q && s.q_size >= _internal_tx_queue_size && _blocking) {
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

static inline int _send_packet(WritablePacket* p, struct netmap_ring* txring, struct netmap_slot* slot, u_int &cur) {
	int n = 1;
#if HAVE_ZEROCOPY
			if (likely(NetmapBufQ::is_netmap_packet(p))) {
				slot->len = p->length();
				if (p->headroom() > 0) {
					complaint++;
					if (complaint < 5)
						click_chatter("Shifting data in ToNetmapDevice. You should avoid this case !");
					p = static_cast<PacketBatch*>(p->shift_data(-p->headroom())); //Is it better to shift or to copy like if it was not a netmap buffer?
				}
#  if HAVE_NETMAP_PACKET_POOL //We must return the netmap buffer to the packet pool
				uint32_t buf_idx = NETMAP_BUF_IDX(txring,p->buffer());

				if (!(slot->flags & NS_NOFREE)) { //But only if it's not shared
					unsigned char * tx_buffer_data = (unsigned char*)NETMAP_BUF(txring,slot->buf_idx);
					static_cast<WritablePacket*>(p)->set_buffer(tx_buffer_data,txring->nr_buf_size);
				}

				slot->buf_idx = buf_idx;

				if (p->data_packet() || p->buffer_destructor() == Packet::empty_destructor) {
					//If it's a shared packet, do not free the buffer
					slot->flags = NS_BUF_CHANGED | NS_NOFREE;
				} else {
					slot->flags = NS_BUF_CHANGED;
				}
#  else //We must return the netmap buffer to the netmap buffer queue
				if (!(slot->flags & NS_NOFREE)) { //But only if it's not shared
					NetmapBufQ::local_pool()->insert(slot->buf_idx);
				}
				slot->buf_idx = NETMAP_BUF_IDX(txring,p->buffer());
				if (p->buffer_destructor() == NetmapBufQ::buffer_destructor) {
					p->reset_buffer();
					slot->flags = NS_BUF_CHANGED;
				} else { //If the buffer destructor is something else, this is a shared netmap packet that we must not release ourselves
					slot->flags = NS_BUF_CHANGED | NS_NOFREE;
				}
#  endif //HAVE_NETMAP_PACKET_POOL
			} else
#endif //HAVE_ZEROCOPY
			{
				unsigned char* srcdata = p->data();
				unsigned length = p->length();

				while (length > txring->nr_buf_size) {
					click_chatter("Warning ! Buffer splitting is highly experimental ! Prefer to send < 2k packets !");
					memcpy((unsigned char*)NETMAP_BUF(txring, slot->buf_idx),srcdata,txring->nr_buf_size);
					srcdata += txring->nr_buf_size;
					length -= txring->nr_buf_size;
					slot->len = txring->nr_buf_size;
					slot->flags |= NS_MOREFRAG;
					srcdata += txring->nr_buf_size;

					n++;
					cur = nm_ring_next(txring,cur);
					slot = &txring->slot[cur];
				}
				memcpy((unsigned char*)NETMAP_BUF(txring, slot->buf_idx),srcdata,length);
				slot->len = length;
			}
		return n;
}

/**
 * Send a linked list of packet, return the number of packet sent and the head
 * 	points toward the packets following the last sent packet (could be null)
 * @arg head First packet of the list
 * @arg ask_sync If true, will force to flush packets (call netmap NIOCTXSYNC) after adding them in the ring
 * @arg txsync_on_empty If true, will do a txsync on all empty queue to force reclaim sent buffers
 *
 * @return The number of packets sent
 */
inline unsigned int ToNetmapDevice::send_packets(Packet* &head, bool ask_sync, bool txsync_on_empty) {
	State &s = state.get();
	struct nm_desc* nmd;
	struct netmap_ring *txring;
	struct netmap_slot *slot;

	WritablePacket* next = static_cast<WritablePacket*>(head);
	WritablePacket* p;

	unsigned int sent = 0;
#if HAVE_BATCH_RECYCLE
        BATCH_RECYCLE_START();
#endif

	for (int iloop = 0; iloop < queue_per_threads; iloop++) {
		int in = (s.last_queue + iloop) % queue_per_threads;
		int i =  queue_for_thisthread_begin() + in;
		nmd = _device->nmds[i];
		txring = NETMAP_TXRING(nmd->nifp, i);

		if (nm_ring_empty(txring)) {
			if (txsync_on_empty)
				try_txsync(i,nmd->fd);
			continue;
		}

#if HAVE_NETMAP_PACKET_POOL
		unsigned int next_slots = 1;
#else
		unsigned int next_slots = ((next->length() - 1) / txring->nr_buf_size) + 1;
#endif

		u_int cur = txring->cur;
		u_int space = nm_ring_space(txring);
		while ((space >= next_slots) && next) {
			p = next;
			next_slots = ((next->length() - 1) / nmd->some_ring->nr_buf_size) + 1;
			next = static_cast<WritablePacket*>(p->next());

			slot = &txring->slot[cur];

			space -= _send_packet(p,txring,slot,cur);

			if (unlikely(cur % 32 == 0)) {
				ask_sync = true;
				slot->flags |= NS_REPORT;
			}

			BATCH_RECYCLE_PACKET_CONTEXT(p);

			sent++;
			cur = nm_ring_next(txring,cur);
#if !HAVE_NETMAP_PACKET_POOL
			if (next)
				next_slots = ((next->length() - 1) / nmd->some_ring->nr_buf_size) + 1;
#endif
		}
		txring->head = txring->cur = cur;

		if (unlikely(ask_sync))
			do_txsync(nmd->fd);
	}

	if (sent == 0) { //Nothing could be sent...
		return 0;
	} else {
#if HAVE_BATCH_RECYCLE
	    BATCH_RECYCLE_END();
#endif
		add_count(sent);
		head = next;
		return sent;
	}
}

void
ToNetmapDevice::selected(int fd, int)
{
	task_for_thread()->reschedule();
	master()->thread(click_current_cpu_id())->select_set().remove_select(fd,this,SELECT_WRITE);
}

bool
ToNetmapDevice::run_task(Task* task)
{
	State &s = state.get();
	unsigned int total_sent = 0;

	Packet* batch = s.q;
	int batch_size = s.q_size;
	s.q = NULL;
	s.q_size = 0;
	do {
#if HAVE_BATCH
		if (!batch || batch_size < _burst) {
			PacketBatch* new_batch = input_pull_batch(0,_burst - batch_size);
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
			s.backoff = 1;
			Packet* last = batch->prev();
			last->set_next(NULL); //Just to be sure

			lock();
			unsigned int sent = send_packets(batch,false,false);
			unlock();

			total_sent += sent;
			batch_size -= sent;

			if (batch) //Reestablish the tail if we could not send everything
				batch->set_prev(last);

			if (sent == 0 || batch) //Not all packets could be sent
				break;
		} else //No packet to send
			break;
	} while (1);

	if (batch != NULL) {/*Output ring is full, we rely on the select mechanism
		to know when we'll have space to send packets*/
		s.q = batch;
		s.q_size = batch_size;
		//Register fd to wait for space
		for (int i = queue_for_thisthread_begin(); i <= queue_for_thisthread_end(); i++) {
			if (_pull_use_select)
				master()->thread(click_current_cpu_id())->select_set().add_select(_device->nmds[i]->fd,this,SELECT_WRITE);
			else
				do_txsync(_device->nmds[i]->fd);
		}
		if (!_pull_use_select)
			task->fast_reschedule();

	} else if (s.signal.active()) {
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
    for (unsigned int i = 0; i < state.weight(); i++) {
        if (state.get_value(i).q) {
        	Packet* next = state.get_value(i).q->next();
        	state.get_value(i).q->kill();
        	state.get_value(i).q = next;
        }
        if (state.get_value(i).timer)
            delete state.get_value(i).timer;
    }
    if (!input_is_pull(0))
        for (int i = firstqueue; i < min((int)_zctimers.size(),firstqueue + n_queues); i++)
            delete _zctimers[i];

    if (_device) _device->destroy();
}


void
ToNetmapDevice::add_handlers()
{
    add_read_handler("count", count_handler, 0);
    add_read_handler("dropped", dropped_handler, 0);
    add_write_handler("reset_counts", reset_count_handler, 0, Handler::BUTTON);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel netmap QueueDevice)
EXPORT_ELEMENT(ToNetmapDevice)
