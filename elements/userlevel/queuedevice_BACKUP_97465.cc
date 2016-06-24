// -*- c-basic-offset: 4; related-file-name: "queuedevice.hh" -*-
/*
 * queuedevice.{cc,hh} -- Base element for multiqueue/multichannel device
 *
 * Copyright (c) 2014 Tom Barbette, University of Liège
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

#include "queuedevice.hh"

CLICK_DECLS

int QueueDevice::n_initialized = 0;
int QueueDevice::n_elements = 0;
int QueueDevice::n_inputs = 0;
int QueueDevice::use_nodes = 0;
Vector<int> QueueDevice::inputs_count = Vector<int>();
Vector<int> QueueDevice::shared_offset = Vector<int>();

QueueDevice::QueueDevice() :  nqueues(1), usable_threads(),
<<<<<<< HEAD
	queue_per_threads(1), queue_share(1), ndesc(-1), _maxthreads(-1),
=======
	queue_per_threads(1), queue_share(1), ndesc(0), _maxthreads(-1),
>>>>>>> 2e7c9f0f15fd35708c02cffbaf231804aa592cc3
	_minqueues(1),_maxqueues(128),_threadoffset(-1),thread_share(1),
	_this_node(0){
#if HAVE_NUMA
	_numa = true;
#else
	_numa = false;
#endif
<<<<<<< HEAD
	_verbose = false;
=======
	_verbose = 1;
>>>>>>> 2e7c9f0f15fd35708c02cffbaf231804aa592cc3
}
void QueueDevice::static_initialize() {
#if HAVE_NUMA
    int num_nodes = Numa::get_max_numas();
    if (num_nodes < 1)
        num_nodes = 1;
#else
    int num_nodes = 1;
#endif
    shared_offset.resize(num_nodes);
    inputs_count.resize(num_nodes);
    inputs_count.fill(0);
    shared_offset.fill(0);
}

int QueueDevice::configure_rx(int numa_node,unsigned int maxthreads, unsigned int minqueues, unsigned int maxqueues, unsigned int threadoffset, ErrorHandler *) {
    _maxthreads = maxthreads;
    _minqueues = minqueues;
    _maxqueues = maxqueues;
    _threadoffset = threadoffset;

    #if !HAVE_NUMA
        (void)numa_node;
        _this_node = 0;
        usable_threads.assign(master()->nthreads(),false);
    #else
        usable_threads.assign(min(Numa::get_max_cpus(), master()->nthreads()),false);
        if (numa_node < 0) numa_node = 0;
        _this_node = numa_node;
    #endif

    if (_maxthreads == -1 || _threadoffset == -1) {
        inputs_count[_this_node] ++;
        if (inputs_count[_this_node] == 1)
            use_nodes++;
    }
    n_elements ++;
    return 0;
}

int QueueDevice::configure_tx(unsigned int maxthreads,unsigned int minqueues, unsigned int maxqueues, ErrorHandler *) {
    _maxthreads = maxthreads;
    _minqueues = minqueues;
    _maxqueues = maxqueues;
    n_elements ++;
    return 0;
}

int QueueDevice::initialize_tx(ErrorHandler * errh) {
    usable_threads.assign(master()->nthreads(),false);
    int n_threads = 0;

    if (input_is_pull(0)) {
        usable_threads[router()->home_thread_id(this)] = 1;
        if (_maxthreads == -1)
            n_threads = 1;
        else
            n_threads = min(_maxthreads,master()->nthreads() - router()->home_thread_id(this));
    } else {
        usable_threads = get_threads();
        if (_maxthreads == -1)
            n_threads = usable_threads.weight();
        else
            n_threads = min(_maxthreads,usable_threads.weight());
    }

    if (n_threads == 0) {
        return errh->error("No threads end up in this queuedevice...? Aborting.");
    }

    nqueues = min(_maxqueues,n_threads);
    nqueues = max(_minqueues,nqueues);

    queue_per_threads = nqueues / n_threads;
    if (queue_per_threads == 0) {
        queue_per_threads = 1;
        thread_share = n_threads / nqueues;
    }

    n_initialized++;
<<<<<<< HEAD
    if (_verbose)
=======
    if (_verbose > 1) {
>>>>>>> 2e7c9f0f15fd35708c02cffbaf231804aa592cc3
		if (input_is_push(0))
			click_chatter("%s : %d threads can end up in this output devices. %d queues will be used, so %d queues for %d thread",name().c_str(),n_threads,nqueues,queue_per_threads,thread_share);
		else
			click_chatter("%s : %d threads will be used to pull packets upstream. %d queues will be used, so %d queues for %d thread",name().c_str(),n_threads,nqueues,queue_per_threads,thread_share);
<<<<<<< HEAD

=======
    }
>>>>>>> 2e7c9f0f15fd35708c02cffbaf231804aa592cc3
    return 0;
}
int QueueDevice::initialize_rx(ErrorHandler *errh) {

	if (router()->thread_sched() && router()->thread_sched()->initial_home_thread_id(this) != ThreadSched::THREAD_UNKNOWN) {
		usable_threads[router()->thread_sched()
					   ->initial_home_thread_id(this)] = 1;
		n_initialized++;
		click_chatter(
				"%s : remove StaticThreadSched to use FastClick's "
				"auto-thread assignment", class_name());
		return 0;
	};

#if HAVE_NUMA
	NumaCpuBitmask b = NumaCpuBitmask::allocate();
    if (numa_available()==0 && _numa) {
        if (_this_node >= 0) {
            b = Numa::node_to_cpus(_this_node);
        } else
            b = Numa::all_cpu();
        b.toBitvector( usable_threads);
    } else
#endif
    {
        usable_threads.negate();
    }

       for (int i = click_nthreads; i < usable_threads.size(); i++)
           usable_threads[i] = 0;

       if (router()->thread_sched()) {
           Bitvector v = router()->thread_sched()->assigned_thread();
           if (v.size() < usable_threads.size())
               v.resize(usable_threads.size());
           if (v.weight() == usable_threads.weight()) {
<<<<<<< HEAD
               if (_verbose)
=======
               if (_verbose > 0)
>>>>>>> 2e7c9f0f15fd35708c02cffbaf231804aa592cc3
                   click_chatter("Warning : input thread assignment will assign threads already assigned by yourself, as you didn't left any cores for %s",name().c_str());
           } else
               usable_threads &= (~v);
       }

       int cores_in_node = usable_threads.weight();

       int n_threads;

       //click_chatter("_maxthreads %d, cores_in_node %d, nthreads() %d, use_nodes %d, _this_node %d, inputs_count %d",_maxthreads,cores_in_node,master()->nthreads(),use_nodes,_this_node,inputs_count[_this_node]);
       if (_maxthreads == -1) {
           n_threads = min(cores_in_node,master()->nthreads() / use_nodes) / inputs_count[_this_node];
       } else {
           n_threads = min(cores_in_node,_maxthreads);
       }

       if (n_threads == 0) {
           n_threads = 1;
           if (cores_in_node == 0) {
               click_chatter("%s : No cores available on the same NUMA node, I'll use a core from another NUMA node, this will reduce performances.",name().c_str());
               usable_threads[0] = 1;
               cores_in_node = 1;
               if (use_nodes > 1)
                   use_nodes = 1;
           }
           thread_share = inputs_count[_this_node] / min(cores_in_node,master()->nthreads() / use_nodes);
       }

       if (n_threads > _maxqueues) {
           queue_share = n_threads / _maxqueues;

       }

       if (_threadoffset == -1) {
           _threadoffset = shared_offset[_this_node];
           shared_offset[_this_node] += n_threads;
       }

       if (thread_share > 1) {
           if (_threadoffset != -1) {
               errh->warning("Thread offset %d will be ignored because the numa node has not enough cores.",_threadoffset);
           }
           _threadoffset = _threadoffset % (inputs_count[_this_node] / thread_share);
       } else
           if (n_threads + _threadoffset > master()->nthreads())
               _threadoffset = master()->nthreads() - n_threads;


       nqueues = min(_maxqueues,n_threads);
       nqueues = max(_minqueues,nqueues);

       queue_per_threads = nqueues / n_threads;

       if (queue_per_threads * n_threads < nqueues) queue_per_threads ++;

       int count = 0;
       int offset = 0;
       for (int b = 0; b < usable_threads.size(); b++) {
           if (count >= n_threads) {
               usable_threads[b] = false;
           } else {
               if (usable_threads[b]) {
                   if (offset < _threadoffset) {
                       usable_threads[b] = false;
                       offset++;
                   } else {
                       count++;
                   }
               }
           }
       }

       if (count < n_threads) {
           return errh->error("Node has not enough threads for device !");
       }

       n_initialized++;

       return 0;
}

int QueueDevice::initialize_tasks(bool schedule, ErrorHandler *errh) {
	_tasks.resize(usable_threads.weight());
	_locks.resize(usable_threads.weight());
	_thread_to_queue.resize(master()->nthreads());
	_queue_to_thread.resize(nqueues);

	int th_num = 0;

	int share_idx = 0;
	for (int th_id = 0; th_id < master()->nthreads(); th_id++) {
		if (!usable_threads[th_id])
			continue;

		if (share_idx % thread_share != 0) {
			--th_num;
			if (_locks[th_num] == NO_LOCK) {
				_locks[th_num] = 0;
			}
		} else {
			_tasks[th_num] = (new Task(this));
			ScheduleInfo::initialize_task(this, _tasks[th_num], schedule, errh);
			_tasks[th_num]->move_thread(th_id);
			_locks[th_num] = NO_LOCK;
		}
		share_idx++;

		_thread_to_queue[th_id] = (th_num * queue_per_threads) / queue_share;

		for (int j = 0; j < queue_per_threads; j++) {
			if (_verbose > 2)
				click_chatter("Queue %d handled by th %d",((th_num * queue_per_threads) + j) / queue_share,th_id);
			_queue_to_thread[((th_num * queue_per_threads) + j) / queue_share] = th_id;
		}

		if (queue_share > 1) {
			_locks[th_num] = 0;
		}

		++th_num;

	}

	return 0;

}

CLICK_ENDDECLS
ELEMENT_PROVIDES(QueueDevice)
