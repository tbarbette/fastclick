// -*- c-basic-offset: 4; related-file-name: "queuedevice.hh" -*-
/*
 * queuedevice.{cc,hh} -- Base element for multiqueue/multichannel device
 * Tom Barbette, Georgios Katsikas
 *
 * Copyright (c) 2014-2018 University of Liège
 * Copyright (c) 2018-2019 KTH Royal Institute of Technology
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

//Number of nodes using automatic thread assignment
int QueueDevice::use_nodes = 0;
Vector<int> QueueDevice::inputs_count = Vector<int>();
Vector<int> QueueDevice::shared_offset = Vector<int>();

QueueDevice::QueueDevice() :
    _minqueues(0),_maxqueues(128), _burst(0),
    _q_infos(), usable_threads(),
    queue_per_threads(1), queue_share(1), ndesc(0), allow_nonexistent(false),
    _maxthreads(-1), _minthreads(0), firstqueue(-1), lastqueue(-1), n_queues(-1),
    thread_share(1), _thread_state(), _this_node(0), _active(true) {
    _verbose = 1;
}

void
QueueDevice::static_initialize() {
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

int QueueDevice::parse(Vector<String> &conf, ErrorHandler *errh) {

    if (Args(this, errh).bind(conf)
           .read_p("QUEUE", firstqueue)
           .read("N_QUEUES",n_queues)
           .read("MAXTHREADS", _maxthreads)
           .read("VERBOSE", _verbose)
           .read("ACTIVE", _active)
           .read("ALLOW_NONEXISTENT", allow_nonexistent)
           .consume() < 0)
        return -1;

    n_elements ++;

    return 0;
}

bool QueueDevice::get_spawning_threads(Bitvector& bmk, bool, int port)
{
    if (noutputs()) { //RX
        //if (_active) { TODO
            for (int i = 0; i < n_queues; i++) {
                for (int j = 0; j < queue_share; j++) {
                    bmk[thread_for_queue_offset(i) - j] = 1;
                }
            }
        // }
        return true;
    } else { //TX
        if (_active) {
            if (input_is_pull(0)) {
                bmk[router()->home_thread_id(this)] = 1;
            }
        }
        return true;
    }
}

void QueueDevice::cleanup_tasks() {
    for (int i = 0; i < _thread_state.weight(); i++) {
        if (_thread_state.get_value(i).task) {
            delete _thread_state.get_value(i).task;
            _thread_state.get_value(i).task = 0;
        }
    }
}

int RXQueueDevice::parse(Vector<String> &conf, ErrorHandler *errh) {
    QueueDevice::parse(conf, errh);

    _promisc = true;
    _vlan_filter = false;
    _vlan_strip = false;
    _vlan_extend = false;
    _lro = false;
    _jumbo = false;

    if (Args(this, errh).bind(conf)
           .read_p("PROMISC", _promisc)
           .read_p("VLAN_FILTER", _vlan_filter)
           .read_p("VLAN_STRIP", _vlan_strip)
           .read_p("VLAN_EXTEND", _vlan_extend)
           .read_p("LRO", _lro)
           .read_p("JUMBO", _jumbo)
           .consume() < 0)
        return -1;

#if HAVE_NUMA
    _use_numa = true;
#else
    _use_numa = false;
#endif
    _threadoffset = -1;
    _set_rss_aggregate = false;
    _set_paint_anno = false;
    String scale;
    bool has_scale = false;
    _scale_parallel = false;
    _numa_node_override = -1;

    if (Args(this, errh).bind(conf)
        .read("NUMA", _use_numa)
        .read("NUMA_NODE", _numa_node_override)
        .read("SCALE", scale).read_status(has_scale)
        .read("THREADOFFSET", _threadoffset)
            .consume() < 0) {
        return -1;
    }

    if (has_scale) {
        if (scale.lower() == "parallel") {
            _scale_parallel = true;
        } else if (scale.lower() == "share") {
            _scale_parallel = false;
        } else {
            return errh->error("Unknown scaling mode %s !",scale.c_str());
        }
    }

#if !HAVE_NUMA
    if (_use_numa) {
        click_chatter("Cannot use numa if --enable-numa wasn't set during compilation time !");
    }
    _use_numa = false;
#endif
    return 0;
}

TXQueueDevice::TXQueueDevice() : _blocking(false), _internal_tx_queue_size(1024) {
}

int TXQueueDevice::parse(Vector<String> &conf, ErrorHandler* errh) {
    QueueDevice::parse(conf, errh);
    Args args(this, errh);
    if (args.bind(conf).read("IQUEUE", _internal_tx_queue_size)
        .read("BLOCKING", _blocking)
        .consume() < 0)
        return -1;
    if ((_internal_tx_queue_size & (_internal_tx_queue_size - 1)) != 0) {
        errh->error("IQUEUE must be a power of 2");
    }
    return 0;
}

int RXQueueDevice::configure_rx(int numa_node, int minqueues, int maxqueues, ErrorHandler *) {
    _minqueues = minqueues;
    _maxqueues = maxqueues;
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
        if (inputs_count[_this_node] == 1) {
            use_nodes++;
        }
    }

    return 0;
}

int TXQueueDevice::configure_tx(int minqueues, int maxqueues, ErrorHandler *) {
    _minqueues = minqueues;
    _maxqueues = maxqueues;
    return 0;
}

int TXQueueDevice::initialize_tx(ErrorHandler * errh) {
    usable_threads.assign(master()->nthreads(),false);
    int n_threads = 0;

    if (input_is_pull(0)) {
        usable_threads[router()->home_thread_id(this)] = 1;
        if (_maxthreads == -1)
            n_threads = 1;
        else
            n_threads = min(_maxthreads, master()->nthreads() - router()->home_thread_id(this));
    } else {
        usable_threads = get_passing_threads();
        if (_maxthreads == -1)
            n_threads = usable_threads.weight();
        else
            n_threads = min(_maxthreads, usable_threads.weight());
    }

    if (n_threads == 0) {
        errh->warning("No threads end up in this queuedevice...?");
    }

    if (n_threads > _maxqueues) {
        queue_share = n_threads / _maxqueues;
    }

    if (n_threads >= _maxqueues)
        n_queues = _maxqueues;
    else
        n_queues = max(_minqueues,n_threads);

    if (n_threads > 0) {
        queue_per_threads = n_queues / n_threads;
        if (queue_per_threads == 0) {
            queue_per_threads = 1;
            thread_share = n_threads / n_queues;
        }
    }
    n_initialized++;
    if (_verbose > 1) {
        if (input_is_push(0))
            click_chatter("%s: %d threads can end up in this output devices. %d queues will be used, so %d queues for %d thread",
                name().c_str(), n_threads, n_queues, queue_per_threads, thread_share);
        else
            click_chatter("%s: %d threads will be used to pull packets upstream. %d queues will be used, so %d queues for %d thread",
                name().c_str(), n_threads, n_queues, queue_per_threads, thread_share);
    }
    return 0;
}

int RXQueueDevice::initialize_rx(ErrorHandler *errh) {
    int n_threads;
    int cores_in_node;
    int count = 0;
    int offset = 0;

    if (router()->thread_sched() && router()->thread_sched()->initial_home_thread_id(this) != ThreadSched::THREAD_UNKNOWN) {
        usable_threads[router()->thread_sched()
            ->initial_home_thread_id(this)] = 1;
        n_threads = 1;
        if (n_queues == -1) {
            if (n_threads >= _maxqueues)
                n_queues = _maxqueues;
            else
                n_queues = max(_minqueues,n_threads);
        }
        queue_per_threads = n_queues / n_threads;
        lastqueue = firstqueue + n_queues - 1;

        click_chatter("%s: remove StaticThreadSched to use FastClick's auto-thread assignment", class_name());
        goto end;
    };

    {
#if HAVE_NUMA
        NumaCpuBitmask b = NumaCpuBitmask::allocate();

        if (numa_available()==0 && _use_numa) {
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
    }

    for (int i = click_max_cpu_ids(); i < usable_threads.size(); i++)
       usable_threads[i] = 0;

    if (router()->thread_sched()) {
       Bitvector v = router()->thread_sched()->assigned_thread();
       if (v.size() < usable_threads.size())
           v.resize(usable_threads.size());
       if (v.weight() == usable_threads.weight()) {
           if (_verbose > 0)
               click_chatter(
                "Warning: input thread assignment will assign threads already assigned by yourself, as you didn't left any cores for %s",
                name().c_str());
       } else
           usable_threads &= (~v);
       if (_threadoffset != -1 && !usable_threads[_threadoffset]) {
           click_chatter(
            "WARNING: The THREADOFFSET parameter will be ignored because that thread is not usable / assigned to another element");
       }
    }

    if (usable_threads.weight() < _minthreads) {
        int miss = _minthreads -usable_threads.weight();
        int i = 0;
        while (miss > 0 && i < usable_threads.size()) {
            if (!usable_threads[i]) {
                miss--;
                usable_threads[i] = true;
            }
            i++;

        }
    }

    cores_in_node = usable_threads.weight();

    //click_chatter("_maxthreads %d, cores_in_node %d, nthreads() %d, use_nodes %d, _this_node %d, inputs_count %d",_maxthreads,cores_in_node,master()->nthreads(),use_nodes,_this_node,inputs_count[_this_node]);
    if (_maxthreads == -1) {
       assert(use_nodes > 0);
       if (_scale_parallel)
           n_threads = cores_in_node;
       else {
           assert(inputs_count[_this_node] > 0);
           n_threads = min(cores_in_node,master()->nthreads() / use_nodes) / inputs_count[_this_node];
       }
    } else {
       n_threads = min(cores_in_node,_maxthreads);
    }

    if (n_threads == 0) {
       n_threads = 1;
       if (cores_in_node == 0) {
           click_chatter(
            "%s: No cores available on the same NUMA node, a core from another NUMA node will be used and this will reduce the performance",
            name().c_str());
           usable_threads[0] = 1;
           cores_in_node = 1;
           if (use_nodes > 1)
               use_nodes = 1;
       }
       if (!_scale_parallel) {
           if (use_nodes == 0)
               use_nodes = 1;
           thread_share = inputs_count[_this_node] / min(cores_in_node,master()->nthreads() / use_nodes);
       } else
           thread_share = min(cores_in_node,master()->nthreads());
       if (thread_share == 0)
           thread_share = 1;
    }

    if (n_threads > _maxqueues) {
       assert(_maxqueues > 0);
       queue_share = n_threads / _maxqueues;
    }

    if (_threadoffset == -1) {
       if (!_scale_parallel) {
           _threadoffset = shared_offset[_this_node];
           shared_offset[_this_node] += n_threads;
       } else {
            _threadoffset = 0;
       }
    }

    if (thread_share > 1) {
       if (_threadoffset != -1) {
           errh->warning("Thread offset %d will be ignored because the numa node has not enough cores", _threadoffset);
       }
       if (!_scale_parallel) {
           assert(thread_share > 0);
           _threadoffset = _threadoffset % (inputs_count[_this_node] / thread_share);
       }
    } else
       if (n_threads + _threadoffset > master()->nthreads())
           _threadoffset = master()->nthreads() - n_threads;

    if (n_threads >= _maxqueues)
       n_queues = _maxqueues;
    else
       n_queues = max(_minqueues,n_threads);

    assert(n_threads > 0);
    queue_per_threads = n_queues / n_threads;

    if (queue_per_threads * n_threads < n_queues) queue_per_threads ++;
    lastqueue = firstqueue + n_queues - 1;

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

    if (_verbose > 1) {
        if (n_threads >= n_queues)
           click_chatter(
            "%s: %d threads will be used to push packets downstream. %d queues will be used meaning that each queue will be shared by %d threads",
            name().c_str(), n_threads, n_queues, queue_share);
        else
           click_chatter(
            "%s: %d threads will be used to push packets downstream. %d queues will be used meaning that each thread will handle up to %d queues",
            name().c_str(), n_threads, n_queues, queue_per_threads);
        }

    end:
        n_initialized++;
        return 0;
}

int QueueDevice::initialize_tasks(bool schedule, ErrorHandler *errh, TaskCallback hook) {
    _q_infos.resize(n_queues);

    int th_num = 0;
    int qu_num = firstqueue;
    if (_verbose > 2)
        click_chatter("%s: using queues from %d to %d", name().c_str(), firstqueue, firstqueue + n_queues - 1);

    //If there is multiple threads per queue, share_idx will be in [0,thread_share[, thread_share being the amount of queues that needs to be shared between threads
    int th_share_idx = 0;
    int qu_share_idx = 0;
    for (int th_id = 0; th_id < master()->nthreads(); th_id++) {
        if (!usable_threads[th_id])
            continue;

        if (th_share_idx % thread_share != 0) {
            --th_num;
            _q_infos[qu_num - firstqueue].lock = 0;
        }

        Task* &task = _thread_state.get_value_for_thread(th_id).task;
        if (hook)
            task = new Task(hook, this);
        else
            task = new Task(this);
        ScheduleInfo::initialize_task(this, task, schedule, errh);
        task->move_thread(th_id);

        th_share_idx++;

        _thread_state.get_value_for_thread(th_id).first_queue_id = qu_num;

        for (int j = 0; j < queue_per_threads; j++) {
            if (_verbose > 2)
                click_chatter("%s: Queue %d handled by th %d", name().c_str(), qu_num,th_id);
            _q_infos[qu_num - firstqueue].thread_id = th_id;
            // If queues are shared, this mapping is loosy: _q_infos[].thread_id will map to the last thread. That's fine, we only want to retrieve one to find one thread to finish some job
            qu_share_idx++;
            if (qu_share_idx % queue_share == 0)
                qu_num++;
            if (qu_num == firstqueue + n_queues) break;
        }

        if (qu_num == firstqueue + n_queues) break;
        ++th_num;
    }

    return 0;
}

unsigned long long QueueDevice::n_count() {
    unsigned long long total = 0;
    for (unsigned int i = 0; i < _thread_state.weight(); i ++) {
        total += _thread_state.get_value(i)._count;
    }
    return total;
}

unsigned long long QueueDevice::n_dropped() {
    unsigned long long total = 0;
    for (unsigned int i = 0; i < _thread_state.weight(); i ++) {
        total += _thread_state.get_value(i)._dropped;
    }
    return total;
}

void QueueDevice::reset_count() {
    for (unsigned int i = 0; i < _thread_state.weight(); i ++) {
        _thread_state.get_value(i)._count = 0;
        _thread_state.get_value(i)._dropped = 0;
    }
}

String QueueDevice::count_handler(Element *e, void *user_data)
{
    QueueDevice *tdd = static_cast<QueueDevice *>(e);
    intptr_t what = reinterpret_cast<intptr_t>(user_data);
    switch (what) {
        case h_count:
            return String(tdd->n_count());
        default:
            return "<undefined>";
    }
}

String QueueDevice::dropped_handler(Element *e, void *)
{
    QueueDevice *tdd = static_cast<QueueDevice *>(e);
    return String(tdd->n_dropped());
}

int QueueDevice::reset_count_handler(const String &, Element *e, void *,
                                ErrorHandler *)
{
    QueueDevice *tdd = static_cast<QueueDevice *>(e);
    tdd->reset_count();
    return 0;
}
CLICK_ENDDECLS
ELEMENT_PROVIDES(QueueDevice)
