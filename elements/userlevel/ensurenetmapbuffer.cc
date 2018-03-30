// -*- c-basic-offset: 4; related-file-name: "ensurenetmapbuffer.hh" -*-
/*
 * ensurenetmapbuffer.{cc,hh} - Ensure Netmap Buffer
 *
 * Copyright (c) 2015 University of Liège
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
#include <click/netmapdevice.hh>
#include <click/args.hh>
#include <click/netmapdevice.hh>
#include "ensurenetmapbuffer.hh"

CLICK_DECLS


EnsureNetmapBuffer::EnsureNetmapBuffer() : _headroom(0)
{
}

EnsureNetmapBuffer::~EnsureNetmapBuffer()
{
}

int
EnsureNetmapBuffer::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
	.read_p("HEADROOM", _headroom)
	.complete() < 0)
    return -1;

    return 0;
}

inline Packet*
EnsureNetmapBuffer::smaction(Packet* p) {
	if (NetmapBufQ::is_netmap_packet(p)) {
		return p;
	} else {
#if HAVE_NETMAP_PACKET_POOL
		WritablePacket* q = WritablePacket::make(p->length());
#else
		unsigned char* buffer = NetmapBufQ::local_pool()->extract_p();
		if (!buffer) {
			p->kill();
			return 0;
		}
		WritablePacket* q = WritablePacket::make(buffer,NetmapBufQ::buffer_size(),NetmapBufQ::buffer_destructor,NetmapBufQ::local_pool());
#endif
		q->copy(p, _headroom);
		p->kill();
		assert(NetmapBufQ::is_netmap_packet(q));
		return q;
	}
}

#if HAVE_BATCH
PacketBatch*
EnsureNetmapBuffer::simple_action_batch(PacketBatch *head)
{
#if HAVE_ZEROCOPY
	EXECUTE_FOR_EACH_PACKET_DROPPABLE(smaction,head,[](Packet* p) {click_chatter("No more netmap buffer ! Dropping packet %p !",p);});
#endif
	return head;
}
#endif
Packet*
EnsureNetmapBuffer::simple_action(Packet* p) {
#if HAVE_ZEROCOPY
	return smaction(p);
#else
	return p;
#endif
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel netmap)
EXPORT_ELEMENT(EnsureNetmapBuffer)
