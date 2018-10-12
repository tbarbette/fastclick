// -*- c-basic-offset: 4; related-file-name: "fromnetmapdevice.hh" -*-
/*
 * fromnetmapdevice.{cc,hh} -- element reads packets live from network via
 * Intel's DPDK
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
#include "fromnetmapdevice.hh"
#include <click/args.hh>
#include <click/master.hh>
#include <click/error.hh>
#include <click/standard/scheduleinfo.hh>
#include <click/packet.hh>
#include <click/packet_anno.hh>
#include <vector>

CLICK_DECLS

FromNetmapDevice::FromNetmapDevice() : _device(NULL),_keephand(false)
{
#if HAVE_BATCH
	in_batch_mode = BATCH_MODE_YES;
#endif
#if HAVE_ZEROCOPY
	NetmapDevice::global_alloc += 2048;
#endif
	_burst = 32;
}

void *
FromNetmapDevice::cast(const char *n)
{
    if (strcmp(n, "FromNetmapDevice") == 0)
	return (Element *)this;
    return NULL;
}

int
FromNetmapDevice::configure(Vector<String> &conf, ErrorHandler *errh)
{

	String flow,ifname;

	int thisnode = 0;

	if (Args(this, errh).bind(conf)
            .read_mp("DEVNAME", ifname)
            .consume() < 0)
        return -1;
	int err = parse(conf, errh);
	if (err != 0)
		return err;

    if (Args(conf, this, errh)
		.read("KEEPHAND",_keephand)
		.complete() < 0)
    	return -1;
#if HAVE_NUMA
    if (_use_numa) {
    const char* device = ifname.c_str();
    thisnode = Numa::get_device_node(&device[7]);
    } else
        thisnode = 0;
#endif

    _device = NetmapDevice::open(ifname);
    if (!_device) {
        return errh->error("Could not initialize %s",ifname.c_str());
    }

    int r;
    if (n_queues == -1) {
        if (firstqueue == -1) {
            firstqueue = 0;
            //By default with Netmap, use all available queues (RSS is enabled by default)
             r = configure_rx(thisnode,_device->n_queues,_device->n_queues,errh);
        } else {
            //If a queue number is setted, user probably want only one queue
            r = configure_rx(thisnode,1,1,errh);
        }
    } else {
        if (firstqueue == -1)
            firstqueue = 0;
        if (firstqueue + n_queues > _device->n_queues)
            return errh->error("You asked for %d queues after queue %d but device only have %d.",n_queues,firstqueue,_device->n_queues);
        r = configure_rx(thisnode,n_queues,n_queues,errh);
    }


    if (r != 0) return r;

    return 0;
}


int
FromNetmapDevice::initialize(ErrorHandler *errh)
{
    int ret;

    ret = initialize_rx(errh);
    if (ret != 0) return ret;

    ret = initialize_tasks(false,errh);
    if (ret != 0) return ret;

	if (_verbose > 0 && thread_per_queues() > 2) {
	errh->warning("Using 3 or more threads per NIC's hardware queue is "
				"discouraged : use more hardware-queue or less threads, or they "
				"will spin to lock the same hardware queue and do nothing usefull.");
	}

	if (_verbose > 0 && queue_per_threads > 3) {
		errh->warning(((usable_threads.weight()==1?String("The thread handling"):
				String("Each thread of")) + String(
						" %s will loop through %d hardware queues. Having "
						"more than 3 queues per thread is useless. Consider limiting the "
						"number of hardware queue of %s (via ethtool -L %s combined X "
						"on Linux), or use N_QUEUES N argument to only use the first N queues and "
						"prevent traffic from going to the last queues by limiting RSS "
						"on %s (via ethtool -X %s equal N).")).c_str(),name().c_str(), queue_per_threads,_device->nmds[0]->nifp->ni_name,_device->nmds[0]->nifp->ni_name,_device->nmds[0]->nifp->ni_name,_device->nmds[0]->nifp->ni_name);
	}

	//IRQ
	char netinfo[100];
	sprintf(netinfo, "/sys/class/net/%s/device/msi_irqs", _device->ifname.c_str() + 7);
	DIR *dir;
	struct dirent *ent;

	int i = 0;
	if ((dir = opendir (netinfo)) != NULL) {
	  while ((ent = readdir (dir)) != NULL && i < firstqueue + n_queues) {
		int n = atoi(ent->d_name);
		if (n == 0) continue;
		if (i < firstqueue) {i++; continue;}

		char irqpath[100];
		int irq_n = n;

		sprintf(irqpath, "/proc/irq/%d/smp_affinity_list", irq_n);
		int fd = open(irqpath, O_WRONLY);
		if (fd <= 0)
			continue;
		sprintf(irqpath,"%d",thread_for_queue(i));
		if (_verbose > 1)
			click_chatter("Pinning IRQ %d (queue %d) to thread %d",irq_n,i,thread_for_queue(i));
		write(fd, irqpath, (size_t)strlen(irqpath));
		close(fd);
		i++;
	  }
	  closedir (dir);
	}

	//Map fd to queues to allow select handler to quickly check the right ring
	_queue_for_fd.resize(_device->_maxfd + 1);
	for (int i = firstqueue; i < n_queues + firstqueue; i++) {
	    int fd = _device->nmds[i]->fd;
	    _queue_for_fd[fd] = i;
	}

	//Register selects for threads
	for (int i = 0; i < usable_threads.size();i++) {
		if (!usable_threads[i])
			continue;
		for (int j = queue_for_thread_begin(i); j <= queue_for_thread_end(i); j++)
			master()->thread(i)->select_set().add_select(_device->nmds[j]->fd,this,SELECT_READ);
	}

	return 0;
}

inline bool
FromNetmapDevice::receive_packets(Task* task, int begin, int end, bool fromtask) {
		unsigned nr_pending = 0;

		int sent = 0;

		for (int i = begin; i <= end; i++) {
			lock();

			struct nm_desc* nmd = _device->nmds[i];

			struct netmap_ring *rxring = NETMAP_RXRING(nmd->nifp, i);

			u_int cur, n;

			cur = rxring->cur;

			n = nm_ring_space(rxring);
			if (_burst > 0 && n > (int)_burst) {
			    nr_pending += n - (int)_burst;
				n = _burst;
			}

			if (n == 0) {
			    unlock();
				continue;
			}

			Timestamp ts = Timestamp::make_usec(nmd->hdr.ts.tv_sec, nmd->hdr.ts.tv_usec);

			sent+=n;

#if HAVE_NETMAP_PACKET_POOL && HAVE_BATCH
			PacketBatch *batch_head = NetmapDevice::make_netmap_batch(n,rxring,cur);
			if (!batch_head) goto error;
#else

	#if HAVE_BATCH
			PacketBatch *batch_head = NULL;
			Packet* last = NULL;
			unsigned int count = n;
	#endif
				while (n > 0) {

					struct netmap_slot* slot = &rxring->slot[cur];

					unsigned char* data = (unsigned char*)NETMAP_BUF(rxring, slot->buf_idx);
					WritablePacket *p;
			#if HAVE_NETMAP_PACKET_POOL
					if (slot->flags & NS_MOREFRAG) {
						click_chatter("Packets bigger than Netmap buffer size are not supported while compiled with Netmap Packet Pool. Please disable this feature.");
						assert(false);
					}
					__builtin_prefetch(data);
					p = WritablePacket::make_netmap(data, rxring, slot);
					if (unlikely(p == NULL)) goto error;
			#else
                #if HAVE_ZEROCOPY
					uint32_t new_buf = 0;
                    if (slot->len > 64 && !(slot->flags & NS_MOREFRAG) && (new_buf = NetmapBufQ::local_pool()->extract())) {
                        __builtin_prefetch(data);
                        p = Packet::make( data, slot->len, NetmapBufQ::buffer_destructor,0);
                        if (!p) goto error;
                        slot->buf_idx = new_buf;
                        slot->flags = NS_BUF_CHANGED;
                    } else
                #endif
                    {
                            if (slot->flags & NS_MOREFRAG) {
                                click_chatter("Packets bigger than Netmap buffer size are not supported for now. Please set MTU lower and disable features like LRO and GRO.");
                                assert(false);
                            }
                        p = Packet::make(data, slot->len);
                        if (!p) goto error;
                    }
            #endif
				p->set_packet_type_anno(Packet::HOST);
				p->set_mac_header(p->data());

	#if HAVE_BATCH
					if (batch_head == NULL) {
						batch_head = PacketBatch::start_head(p);
					} else {
						last->set_next(p);
					}
					last = p;
	#else
					p->set_timestamp_anno(ts);
					output(0).push(p);
    #endif
					cur = nm_ring_next(rxring, cur);
					n--;
				}
#if HAVE_BATCH
				if (batch_head) {
					batch_head->make_tail(last,count);
				}
#endif

#endif
			rxring->head = rxring->cur = cur;
			unlock();
#if HAVE_BATCH
			batch_head->set_timestamp_anno(ts);
			output_push_batch(0,batch_head);
#endif

		}

	if ((int)nr_pending > _burst) { //TODO size/4 or something
	    if (fromtask) {
	            task->fast_reschedule();
	    } else {

	        task->reschedule();
	    }
	}

	add_count(sent);
  return sent;
  error: //No more buffer

  click_chatter("No more buffers !");
  router()->master()->kill_router(router());
  return 0;


}

void
FromNetmapDevice::selected(int fd, int)
{
	receive_packets(task_for_thread(),queue_for_fd(fd),queue_for_fd(fd),false);
}

void
FromNetmapDevice::cleanup(CleanupStage)
{
    cleanup_tasks();
    if (_device) _device->destroy();
}

bool
FromNetmapDevice::run_task(Task* t)
{
    return receive_packets(t,queue_for_thisthread_begin(),queue_for_thisthread_end(),true);

}

void FromNetmapDevice::add_handlers()
{
    add_read_handler("count", count_handler, 0);
    add_read_handler("dropped", dropped_handler, 0);
    add_write_handler("reset_counts", reset_count_handler, 0, Handler::BUTTON);
}



CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel netmap QueueDevice)
EXPORT_ELEMENT(FromNetmapDevice)
ELEMENT_MT_SAFE(FromNetmapDevice)
