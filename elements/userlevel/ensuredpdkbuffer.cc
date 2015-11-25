// -*- c-basic-offset: 4; related-file-name: "ensuredpdkbuffer.hh" -*-
/*
 * ensuredpdkbuffer.{cc,hh} - Ensure DPDK Buffer
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
#include <click/dpdkdevice.hh>
#include "ensuredpdkbuffer.hh"

CLICK_DECLS


EnsureDPDKBuffer::EnsureDPDKBuffer()
{
}

EnsureDPDKBuffer::~EnsureDPDKBuffer()
{
}

inline Packet*
EnsureDPDKBuffer::smaction(Packet* p) {
	if (DPDKDevice::is_dpdk_packet(p)) {
		return p;
	} else {
		struct rte_mbuf* mbuf = DPDKDevice::get_pkt();
		if (!mbuf) {
			p->kill();
			return 0;
		}
		WritablePacket* q = WritablePacket::make((unsigned char*)mbuf->buf_addr,DPDKDevice::DATA_SIZE,DPDKDevice::free_pkt,(void*)mbuf);
		q->copy(p,rte_pktmbuf_headroom(mbuf));
		p->kill();
		return q;
	}
}

#if HAVE_BATCH
PacketBatch*
EnsureDPDKBuffer::simple_action_batch(PacketBatch *head)
{
#if HAVE_ZEROCOPY
	EXECUTE_FOR_EACH_PACKET_DROPPABLE(smaction,head,[](Packet* p) {click_chatter("No more netmap buffer ! Dropping packet %p !",p);});
#endif
	return head;
}
#endif
Packet*
EnsureDPDKBuffer::simple_action(Packet* p) {
#if HAVE_ZEROCOPY
	return smaction(p);
#else
	return p;
#endif
}




CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel dpdk)
EXPORT_ELEMENT(EnsureDPDKBuffer)
