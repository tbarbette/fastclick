// -*- c-basic-offset: 4 -*-
/*
 * flowworkpackage.{cc,hh} --
 * Tom Barbette
 *
 * Copyright (c) 2019 KTH
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
#include "flowworkpackage.hh"
#include <click/args.hh>
#include <click/error.hh>
#include <click/ipflowid.hh>
#include <rte_hash.h>
#include <rte_hash_crc.h>
#include <click/sync.hh>
#include <click/dpdk_glue.hh>
#include <clicknet/tcp.h>

CLICK_DECLS




FlowWorkPackage::FlowWorkPackage() : _stats()
{
}

int
FlowWorkPackage::configure(Vector<String> &conf, ErrorHandler *errh)
{
    int s;
    if (Args(conf, this, errh)
        .read_or_set("PREAD",_read_per_packet,1)
		.read_or_set("PWRITE",_write_per_packet,0)
		.read_or_set("FREAD",_read_per_flow,0)
		.read_or_set("FWRITE",_write_per_flow,1)
		.read_or_set("F",_flow_state_size_user, 4)
		.read_or_set("TABLE_SIZE", _table_size, 65536 * 1024) //65M
		.read_or_set("SEQUENTIAL", _sequential, false)
		.read_or_set("ORDER", _order, -1)
		.read_or_set("W", _w, 0)
        .read_or_set("ACTIVE", _active, true)
        .complete() < 0)
        return -1;

    _flow_state_size_full = _flow_state_size_user;

    if (_sequential)
    	_flow_state_size_full += sizeof(Spinlock);
    if (_order >= 0)
    	_flow_state_size_full += sizeof(tcp_seq_t);
    return 0;
}

int
FlowWorkPackage::initialize(ErrorHandler *errh)
{
	struct rte_hash_parameters hash_params = {0};
	hash_params.name = "flow_work_table";
	hash_params.entries = _table_size;
	hash_params.key_len = sizeof(IPFlow5ID);
	hash_params.hash_func = ipv4_hash_crc;
	hash_params.hash_func_init_val = 0;
	hash_params.extra_flag = RTE_HASH_EXTRA_FLAGS_MULTI_WRITER_ADD | RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY; //| RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY_LF
    _table = rte_hash_create(&hash_params);

    if (!_table)
    	return errh->error("Could not init flow table !");

    _table_data = CLICK_LALLOC(_flow_state_size_full * _table_size);
    if (!_table_data)
    	return errh->error("Could not init data table !");

    if (_sequential) {
		for (int i = 0; i < _table_size; i++) {
			unsigned char* data = &((unsigned char*)_table_data)[i * _flow_state_size_full];
			Spinlock* lock = (Spinlock*)data;
			new(lock) Spinlock();
		}
    }

    return 0;
}


void
FlowWorkPackage::rmaction(Packet* p, int &n_data) {
	if (!_active) {
        unsigned r;
        for (int i = 0; i < _w; i ++) {
            r = (*_gens)();
        }
        return;
    }
    volatile unsigned char* data;
	IPFlow5ID fid = IPFlow5ID(p);
	bool first = false;
	char stack[_flow_state_size_user];

	int ret = rte_hash_lookup(_table, &fid);

	if (ret < 0) { //new flow
		ret = rte_hash_add_key(_table, &fid);
		if (ret < 0) {
			click_chatter("Cannot add key (have %d items)!", rte_hash_count(_table));
			return;
		}
		first = true;
	}

	data = &((volatile unsigned char*)_table_data)[ret * _flow_state_size_full];

	if (_sequential) {
		Spinlock* lock = (Spinlock*)data;
		lock->acquire();
		data += sizeof(Spinlock);
	}

	if (_order >= 0) {
		tcp_seq_t* seq = (tcp_seq_t*)(p->data() + _order);
		tcp_seq_t* last = (tcp_seq_t*)(data);
		if (first) {

		} else {
			if (*seq < *last) {
		        //click_chatter("OO SEQ %d, LAST %d", *seq, *last);
				_stats->out_of_order++;
			}
		}
		*last = *seq;
		data += sizeof(tcp_seq_t);
	}


	if (first) {
		for (int i = 0; i < _write_per_flow; i++) {
			memcpy((void*)data, p->network_header(), _flow_state_size_user);
		}

		for (int i = 0; i < _read_per_flow; i++) {
			memcpy(stack, stack, _flow_state_size_user);
		}
	}
	for (int i = 0; i < _read_per_packet; i++) {
		memcpy(stack, (void*)data, _flow_state_size_user);
	}
	unsigned r;
    for (int i = 0; i < _w; i ++) {
        r = (*_gens)();
    }

	for (int i = 0; i < _write_per_packet; i++) {
		for (int j = 0; j < _flow_state_size_user / sizeof(uint32_t); j++)
			*((volatile uint32_t*)data + j)  =  *((uint32_t*)p->network_header() + j);
	}

	if (_sequential) {
		if (_order >= 0) {
			data -= sizeof(tcp_seq_t);
		}
		data -= sizeof(Spinlock);
		Spinlock* lock = (Spinlock*)data;
		lock->release();
	}

}

#if HAVE_BATCH
void
FlowWorkPackage::push_batch(int port, PacketBatch* batch) {
    int n_data = 0;
    FOR_EACH_PACKET(batch, p)
            rmaction(p,n_data);
    output_push_batch(port, batch);
}
#endif

void
FlowWorkPackage::push(int port, Packet* p) {
    int n_data = 0;
    rmaction(p,n_data);
    output_push(port, p);
}





enum { H_OUTOFORDER };

String
FlowWorkPackage::read_handler(Element *e, void *thunk)
{
	FlowWorkPackage *c = (FlowWorkPackage *)e;

	PER_THREAD_MEMBER_SUM(uint64_t,out_of_order,c->_stats,out_of_order);
    switch ((intptr_t)thunk) {
    case H_OUTOFORDER:
        return String(out_of_order);
    default:
        return "<error>";
    }
}



void
FlowWorkPackage::add_handlers()
{
    add_read_handler("outoforder", FlowWorkPackage::read_handler, H_OUTOFORDER);
}




CLICK_ENDDECLS
ELEMENT_REQUIRES(dpdk dpdk17)
EXPORT_ELEMENT(FlowWorkPackage)
