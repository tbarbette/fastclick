/*
 * iprewritermap.{cc,hh} -- element maps and rewrite IPs to a subnet
 * Tom Barbette
 *
 * Copyright (c) 2015 University of Liege
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
#include "iprewritermap.hh"
#include <click/args.hh>
#include <click/error.hh>

IPRewriterMap::IPRewriterMap()
{

}

IPRewriterMap::~IPRewriterMap()
{
}


int
IPRewriterMap::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
	.read_mp("BEGIN",_begin)
	.read_mp("END",_end)
	.complete() < 0)
	return -1;

  return 0;
}

int
IPRewriterMap::initialize(ErrorHandler *errh)
{
	click_random_srandom();
	_begin = ntohl(_begin);
	_end = ntohl(_end);
	int n = _end - _begin;

	if (n <= 0)
		return errh->error("%x is bigger or equal than %x",_begin,_end);

	_dest.resize(n,IPAddress(0));
	for (int i = 0; i < n; i++) {
		_dest[i] = _begin + i;
	}
	_dest.shuffle(n*10);
	_current_dest = 0;
	return 0;
}

void
IPRewriterMap::cleanup(CleanupStage)
{

}


void
IPRewriterMap::push(int port,Packet *b)
{
	WritablePacket* p = b->uniqueify();
	IPAddress a;
	if (port == 1) {
		a = ntohl(p->ip_header()->ip_src.s_addr);
	} else {
		a = ntohl(p->ip_header()->ip_dst.s_addr);
	}
	int d = _map.find(a,-1);
	if (d == -1) {
		int n_d = _current_dest.fetch_and_add(1);
		_set_lock.acquire();
		click_read_fence();
		HashMap<IPAddress, int >::Pair* pair = _map.find_pair_force(a);
		if (pair->value == 0) {
			pair->value = n_d;
			d = n_d;
		} else {
			d = pair->value;
		}
		_set_lock.release();
	}
	IPAddress n_a = _dest[d];
	if (port == 1) {
		p->ip_header()->ip_src = IPAddress(htonl(n_a));
	} else {
		p->ip_header()->ip_dst = IPAddress(htonl(n_a));
	}
	p->ip_header()->ip_sum = 0;
	p->ip_header()->ip_sum = click_in_cksum((unsigned char *)p->ip_header(), p->ip_header()->ip_hl << 2);
	output(port).push(p);

}

void
IPRewriterMap::add_handlers()
{
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IPRewriterMap)
