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

ToNetmapDevice::ToNetmapDevice() : _block(0), _internal_queue(512), _burst(32)
{

}


int
ToNetmapDevice::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String ifname;

    if (Args(conf, this, errh)
    .read_mp("DEVNAME", ifname)
  	.read_p("IQUEUE",_internal_queue)
	.read_p("BLOCKANT",_block)
  	.read("BURST", _burst)
  	.complete() < 0)
    	return -1;

    int maxthreads = -1;

    if (_internal_queue < _burst * 2) {
        return errh->error("IQUEUE (%d) must be at least twice the size of BURST (%d)!",_internal_queue, _burst);
    }

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

    //If full push, and a thread is assign, we set it as the only thread which can push packets to this element
    if (!input_is_pull(0) && router()->thread_sched() != NULL && router()->thread_sched()->initial_home_thread_id(this) != ThreadSched::THREAD_UNKNOWN) {
    	int thread_id = router()->thread_sched()->initial_home_thread_id(this);
    	click_chatter("Element %s is assigned to thread %d : double check that only this thread will push packets to this element !\n", name().c_str(),thread_id); // Will be auto-detected by bitvector
        usable_threads.assign(master()->nthreads(),false);
        usable_threads[thread_id] = true;
    } else {
    	int thread_id = this->home_thread()->thread_id();
    	usable_threads.assign(master()->nthreads(),false);
    	usable_threads[thread_id] = true;
    }

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
		_queues.resize(nthreads);
		for (int i = 0; i < nqueues; i++) {
			master()->thread(thread_for_queue(i))->select_set().add_select(_device->nmds[i]->fd,this,SELECT_WRITE);
		}

		int nt = 0;
		for (int i = 0; i < nthreads; i++) {
			if (!usable_threads[i]) continue;
			_queues[i] = 0;
			state.get_value_for_thread(i).signal = (Notifier::upstream_empty_signal(this, 0, _tasks[nt]));
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
	master()->thread(click_current_processor())->select_set().remove_select(fd,this,SELECT_WRITE);
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
    if (!_iodone[queue]) {
        do_txsync(fd);
        _iodone[queue] = true;
    }
}

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
			s.q->prev()->set_next(p);
			s.q->set_prev(p);
			s.q_size++;
		} else {
			//Todo : blockant
			add_dropped(1);
			p->kill();
		}
	}

	if (s.q_size >= _burst) { //TODO : or timeout

		Packet* last = s.q->prev();

		/*As we arrive here once every packet, we just try to take the lock,
			if we can't grab it, we'll simply re-try at the next packet*/
		if (lock_attempt()) {
			s.q->prev()->set_next(NULL);
			//If it failed too much... We'll spinlock
		} else if (unlikely(s.q_size > 2*_burst)) {
			s.q->prev()->set_next(NULL);
			lock();
		} else {
			//"Failed lock but not too much packets are waiting
			return;
		}

		//Lock is acquired
		unsigned int sent = send_packets(s.q,true);


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

	}
}

/*Timer for push mode. It will raise the IODONE flag when running to allow a new
 * full synchronization of the ring to be done.*/
void
ToNetmapDevice::run_timer(Timer *) {
    allow_txsync();
}

/**
 * Send a linked list of packet
 *
 * @return The number of packets sent
 */
inline unsigned int ToNetmapDevice::send_packets(Packet* &head, bool push) {
	State &s = state.get();
	struct nm_desc* nmd;
	struct netmap_ring *txring;
	struct netmap_slot *slot;

	WritablePacket* next = head->uniqueify();
	WritablePacket* p = next;

	bool dosync = false;
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

		while ((cur != txring->tail) && next) {
			p = next;

			next = static_cast<WritablePacket*>(p->next());

			slot = &txring->slot[cur];
			slot->len = p->length();

#if HAVE_ZEROCOPY
			if (likely(NetmapBufQ::is_netmap_packet(p))) {
				((NetmapBufQ*)(p->destructor_argument()))->insert(slot->buf_idx);
				slot->buf_idx = NETMAP_BUF_IDX(txring,p->buffer());
				slot->flags |= NS_BUF_CHANGED;
				p->set_buffer_destructor(NetmapBufQ::buffer_destructor_fake);
			} else
#endif
			{
				unsigned char* dstdata = (unsigned char*)NETMAP_BUF(txring, slot->buf_idx);
				void* srcdata = (void*)(p->data());
				memcpy(dstdata,srcdata,p->length());
			}

			p->kill();
			sent++;
			cur = nm_ring_next(txring,cur);
		}

		txring->head = txring->cur = cur;

		if (unlikely(dosync))
			do_txsync(nmd->fd);

		if (next == NULL) { //All is sent
			add_count(sent);
			head = NULL;
			return sent;
		}
	}

	if (next == head) { //Nothing could be sent...
		return 0;
	} else {
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
		/* Difference from vanilla is that we build a batch up to _burst size
		 * and then process it. */
		if (!batch || batch_size < _burst) { //Create a batch up to _burst size, or less if no packets are available
			Packet* last;
			if (!batch) {
				if ((batch = input(0).pull()) == NULL) {
					//TODO : if no signal, we should set a timer
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
		s.q = batch;
		s.q_size = batch_size;
		//Register fd to wait for space
		for (int i = queue_for_thread_begin(); i <= queue_for_thread_end(); i++) {
			master()->thread(click_cpu_id())->select_set().add_select(_device->nmds[i]->fd,this,SELECT_WRITE);
		}
	} else if (s.signal.active()) { //TODO is this really needed?
		//We sent everything we could, but check signal to see if packet arrived after last read
		task->fast_reschedule();
	}
	return total_sent;
}

extern int nthreads;


void
ToNetmapDevice::cleanup(CleanupStage)
{
    for (unsigned int i = 0; i < state.size(); i++) {
        if (state.get_value(i).q) {
        	Packet* next = state.get_value(i).q->next();
        	state.get_value(i).q->kill();
        	state.get_value(i).q = next;
        }
    }
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
