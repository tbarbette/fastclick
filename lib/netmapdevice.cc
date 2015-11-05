// -*- c-basic-offset: 4; related-file-name: "netmapdevice.hh" -*-
/*
 * netmapdevice.{cc,hh} -- library for interfacing with Netmap
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
#include <click/master.hh>
#include <click/standard/scheduleinfo.hh>
#include <click/error.hh>
#include <click/netmapdevice.hh>

#if HAVE_NET_NETMAP_H

#include <stdio.h>
#include <stdint.h>
#include <dirent.h>
#include <fcntl.h>
#include <algorithm>
#include <fstream>

CLICK_DECLS



NetmapDevice::NetmapDevice(String ifname) : _minfd(INT_MAX),_maxfd(INT_MIN),ifname(ifname),_use_count(0) {
	n_refs = 0;
}

NetmapDevice::~NetmapDevice() {
    nics.remove(ifname);

#if HAVE_ZEROCOPY
    if (NetmapDevice::nics.empty()) {

    	uint32_t idx = NetmapBufQ::cleanup();
        if (idx != 0 && parent_nmd) {
        	click_chatter("Releasing packet idx %d",idx);
        	parent_nmd->nifp->ni_bufs_head = idx;
        }
    }
#endif

	click_chatter("Deleting NIC %s",ifname.c_str());
	if (parent_nmd) {
		for (int i = 0; i < n_queues; i++) {
			nm_close(nmds[i]);
			nmds[i] = NULL;
		}

		nm_close(parent_nmd);
		parent_nmd = NULL;
	} else click_chatter("No parent");
}


int NetmapDevice::initialize() {
	    struct nm_desc* nmd;
	    struct nm_desc* base_nmd = (struct nm_desc*)calloc(1,sizeof(struct nm_desc));

		base_nmd->self = base_nmd;
		strcpy(base_nmd->req.nr_name,&(ifname.c_str()[7]));
		base_nmd->req.nr_flags = NR_REG_SW;
	    if (NetmapDevice::some_nmd != NULL) { //Having same netmap space is a lot easier...
	    	base_nmd->mem = NetmapDevice::some_nmd->mem;
	    	base_nmd->memsize = NetmapDevice::some_nmd->memsize;
	    	base_nmd->req.nr_arg2 = NetmapDevice::some_nmd->req.nr_arg2;
	    	base_nmd->done_mmap = NetmapDevice::some_nmd->done_mmap;
	    	NetmapDevice::some_nmd->req.nr_flags = NR_REG_SW;
	    	nmd = nm_open(ifname.c_str(), NULL, NM_OPEN_NO_MMAP | NM_OPEN_IFNAME, base_nmd);
	    } else {
			base_nmd->req.nr_arg3 = _global_alloc;
	    	nmd = nm_open(ifname.c_str(), NULL, NM_OPEN_IFNAME | NM_OPEN_ARG3, base_nmd);
	    	NetmapDevice::some_nmd = nmd;
	    }
	    if (!nmd)
	        return -1;
	    parent_nmd = nmd;

	    if (parent_nmd->nifp->ni_name[0] == '\0')
	        strcpy(parent_nmd->nifp->ni_name,&(ifname.c_str()[7]));

		if (nmd == NULL) {
			click_chatter("Unable to open %s: %s", ifname.c_str(), strerror(errno));
			return 1;
		}

		click_chatter("%s ifname %s\n", __FUNCTION__, ifname.c_str());

	    //Allocate packet pools
	#if HAVE_ZEROCOPY
		if (NetmapBufQ::initialize(NetmapDevice::some_nmd) != 0) {
			nm_close(nmd);
			return -1;
		}
	#endif
		click_chatter("Allocated %d packets",nmd->req.nr_arg3);
		if (nmd->req.nr_arg3) {
			NetmapBufQ::get_global_pool()->insert_all(nmd->nifp->ni_bufs_head);
			nmd->req.nr_arg3 = 0;
		}
		n_queues = nmd->nifp->ni_rx_rings;

		nmds.resize(n_queues);

		for (int i = 0; i < n_queues; i++) {

			struct nm_desc child_nmd = *nmd; //Copy mem, arg2, ...
			child_nmd.self = &child_nmd;
			child_nmd.req.nr_flags = NR_REG_ONE_NIC;
			child_nmd.req.nr_ringid =  i | NETMAP_NO_TX_POLL;

			int flags =NM_OPEN_IFNAME | NM_OPEN_NO_MMAP;

			struct nm_desc* thread_nm = nm_open(ifname.c_str(), NULL, flags,  &child_nmd);

			if (thread_nm == NULL) {
				return -1;
			}
			if (thread_nm->fd < _minfd)
				_minfd = thread_nm->fd;
			if (thread_nm->fd > _maxfd)
				_maxfd = thread_nm->fd;

			nmds[i] = thread_nm;

			struct netmap_ring* txring = NETMAP_TXRING(thread_nm->nifp, i);
			for (int j = 0; j <  txring->num_slots; j++) {
				txring->slot[j].ptr = -1;
			}
		}

		if (base_nmd != NULL) {
			free(base_nmd);
			base_nmd = NULL;
		}

		return 0;


}

HashMap<String,NetmapDevice*> NetmapDevice::nics;
struct nm_desc* NetmapDevice::some_nmd = 0;
per_thread<NetmapBufQ*> NetmapBufQ::netmap_buf_pools = per_thread<NetmapBufQ*>(0,0);
NetmapBufQ* NetmapBufQ::netmap_global_buf_pool = 0;

#if NETMAP_HAVE_DYNAMIC_BUFFER
int NetmapDevice::_global_alloc = 0;
#else
int NetmapDevice::_global_alloc = 2048;
#endif

unsigned int NetmapBufQ::buf_size = 0;
unsigned char* NetmapBufQ::buf_end = 0;
unsigned char* NetmapBufQ::buf_start = 0;
uint32_t NetmapBufQ::max_index = 0;

CLICK_ENDDECLS
#endif
