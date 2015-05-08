// -*- c-basic-offset: 4; related-file-name: "pollqueue.hh" -*-
/*
 * pollqueue.{cc,hh} --
 */

#include <click/config.h>
#include "fromnetmapdevice.hh"
#include <click/args.hh>
#include <click/master.hh>
#include <click/error.hh>
#include <click/standard/scheduleinfo.hh>
#include <click/packet_anno.hh>
#include <vector>

CLICK_DECLS

FromNetmapDevice::FromNetmapDevice() : _device(NULL), _promisc(1),_blockant(false),_burst(0),_keephand(false)
{
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
	int threadoffset = -1;
	int maxthreads = -1;
	int maxqueues = 128;
	bool numa = true;
	int thisnode = 0;

    if (Args(conf, this, errh)
    .read_mp("DEVNAME", ifname)
  	.read_p("PROMISC", _promisc)
  	.read_p("BURST", _burst)
  	.read("MAXTHREADS", maxthreads)
  	.read("THREADOFFSET", threadoffset)
  	.read("KEEPHAND",_keephand)
  	.read("MAXQUEUES",maxqueues)
  	.read("NUMA",numa)

  	.complete() < 0)
    	return -1;
#if !HAVE_NUMA
    if (numa) {
    	click_chatter("Cannot use numa if --enable-numa wasn't set during compilation time !");
    	return -1;
    }
#else
    const char* device = ifname.c_str();
    thisnode = Numa::get_device_node(&device[7]);
#endif

    _device = NetmapDevice::open(ifname);
    if (!_device) {
        return errh->error("Could not initialize %s",ifname.c_str());
    }
    int r = configure_rx(thisnode,maxthreads,_device->n_queues,std::min(maxqueues,_device->n_queues),threadoffset,errh);
    if (r != 0) return r;

    return 0;
}


int
FromNetmapDevice::initialize(ErrorHandler *errh)
{
    int ret;

    ret = QueueDevice::initialize_rx(errh);
    if (ret != 0) return ret;

    ret = QueueDevice::initialize_tasks(false,errh);
    if (ret != 0) return ret;

	//IRQ
	char netinfo[100];
	sprintf(netinfo, "/sys/class/net/%s/device/msi_irqs", _device->parent_nmd->nifp->ni_name);
	DIR *dir;
	struct dirent *ent;
	int max=INT_MIN,min=INT_MAX;
	int n_irqs = 0;

	int i =0;
	if ((dir = opendir (netinfo)) != NULL) {
	  /* print all the files and directories within directory */
	  while ((ent = readdir (dir)) != NULL) {
		int n = atoi(ent->d_name);
		if (n == 0) continue;


		char irqpath[100];
		int irq_n = n;

		sprintf(irqpath, "/proc/irq/%d/smp_affinity_list", irq_n);
		int fd = open(irqpath, O_WRONLY);
		if (fd <= 0)
			continue;
		sprintf(irqpath,"%d",thread_for_queue(i));
		click_chatter("echo %d > %d (%d)",thread_for_queue(i),irq_n,i);
		write(fd, irqpath, (size_t)strlen(irqpath));
		close(fd);
		i++;
		if (i == nqueues)
			break;

	  }
	  closedir (dir);
	}

	//Register select for all concerned threads
	_queue_for_fd.resize(_device->_maxfd + 1);
	for (int i = 0; i < nqueues; i++) {
	    int fd = _device->nmds[i]->fd;
	    _queue_for_fd[fd] = i;
	    for (int j = 0; j < queue_share;j++) {
	        master()->thread(thread_for_queue(i) - j)->select_set().add_select(fd,this,SELECT_READ);
	    }
	}

    //Wait for Netmap init if we're the last interface
    if (all_initialized()) {
        click_chatter("Waiting 3 sec for full hardware initialization of %s...\n",_device->parent_nmd->nifp->ni_name);
        sleep(3);
    }

	return 0;
}

inline bool
FromNetmapDevice::receive_packets(Task* task, int begin, int end, bool fromtask) {
		int nresched = 0;

		int sent = 0;

		for (int i = begin; i <= end; i++) {
		    lock();

			struct nm_desc* nmd = _device->nmds[i];

			struct netmap_ring *rxring = NETMAP_RXRING(nmd->nifp, i);

			u_int cur, n;

			cur = rxring->cur;

			n = nm_ring_space(rxring);
			if (_burst && n > _burst) {
			    nresched += n - _burst;
				n = _burst;
			}

			if (n == 0) {
			    unlock();
				continue;
			}

			Timestamp ts = Timestamp::make_usec(nmd->hdr.ts.tv_sec, nmd->hdr.ts.tv_usec);

			sent+=n;

#if HAVE_NETMAP_PACKET_POOL && HAVE_BATCH
			PacketBatch *batch_head = WritablePacket::make_netmap_batch(n,rxring,cur);
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
					__builtin_prefetch(data);
					p = WritablePacket::make_netmap(data, rxring, slot);
					if (unlikely(p == NULL)) goto error;
			#else
                #if HAVE_ZEROCOPY
                    if (slot->len > 64)
                        __builtin_prefetch(data);
                        p = Packet::make( data, slot->len, NetmapBufQ::buffer_destructor,*netmap_buf_pools);
                        if (!p) goto error;
                        slot->buf_idx = (*netmap_buf_pools)->extract();

                    } else
                #endif
                    {

                            p = Packet::make(data, slot->len);
                            if (!p) goto error;
                    }
            #endif
				p->set_packet_type_anno(PacketType::HOST);

	#if HAVE_BATCH
					if (batch_head == NULL) {
						batch_head = PacketBatch::start_head(p);
					} else {
						last->set_next(p);
					}
					last = p;
	#else
					output(0).push(p);
    #endif
					slot->flags |= NS_BUF_CHANGED;

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
			output(0).push_batch(batch_head);
#endif

		}

	if (nresched > _burst) { //TODO size/4
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
    if (_device) _device->destroy();
}

bool
FromNetmapDevice::run_task(Task* t)
{
    return receive_packets(t,queue_for_thread_begin(),queue_for_thread_end(),true);

}

void FromNetmapDevice::add_handlers()
{
    add_read_handler("count", count_handler, 0);
    add_write_handler("reset_counts", reset_count_handler, 0, Handler::BUTTON);
}



CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel netmap QueueDevice)
EXPORT_ELEMENT(FromNetmapDevice)
ELEMENT_MT_SAFE(FromNetmapDevice)
