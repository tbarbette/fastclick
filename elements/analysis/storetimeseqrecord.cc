/*
 * storetimeseqrecord.{cc,hh} -- Store timestamp annotations into TCP/UDP/ICMP packets.
 * Supports computational batching.
 * by Georgios Katsikas
 *
 * Copyright (c) 2016 KTH Royal Institute of Technology
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
#include <click/glue.hh>
#include <clicknet/ip.h>
#include <clicknet/ip6.h>
#include <clicknet/udp.h>
#include <clicknet/tcp.h>
#include <clicknet/icmp.h>
#include <click/args.hh>
#include <click/error.hh>

#include "storetimeseqrecord.hh"

CLICK_DECLS

/*
 */
StoreTimeSeqRecord::StoreTimeSeqRecord() {
	_count = 0;
	_delta = 0;
	_offset = 0;
}

int
StoreTimeSeqRecord::configure(Vector<String> &conf, ErrorHandler *errh) {
	if (Args(conf, this, errh)
		.read_mp("OFFSET", _offset)
		.read("DELTA", _delta)
		.complete() < 0)
		return -1;
	return 0;
}

// This is the tricky bit.  We can't rely on any headers being
// filled out if elements like CheckIPHeader have not been called
// so we should just access the raw data and cast wisely
Packet*
StoreTimeSeqRecord::simple_action(Packet *packet) {
	WritablePacket *p = packet->uniqueify();
	Timestamp      tnow;
	PData          *pData;
	uint32_t       csum = 0;
	uint32_t       offset = _offset;

	// the packet is shared and we can't modify it without screwing
	// things up
	if (!p) {
	// If uniqueify() fails, packet itself is garbage and has been deleted
		click_chatter("[%s] Non-Writable Packet!", class_name());
		return 0;
	}

	// get the first two words of the IP header to see what it is to determine how to proceed further
	if (p->length() < offset + sizeof(uint32_t) * 2) {
		click_chatter("[%s] Not enough space to proceed", class_name());
		p->kill();
		return 0;
	}

	// Abstract pointer that points to TCP/UDP/ICMP packets
	void           *hdr_pointer = 0;
	unsigned short tr_offset    = -1;
	unsigned short proto        = -1;

	// here need to get to the right offset.  The IP header can be of variable length due to options
	// also, the header might be IPv4 of IPv6
	u_char version = p->data()[offset] >> 4;
	if (version == 0x04) {
		const click_ip *ip = reinterpret_cast<const click_ip *>(p->data() + offset);
		offset += ip->ip_hl << 2;

		// ICMP
		if ( ip->ip_p == IP_PROTO_ICMP ) {
			hdr_pointer = reinterpret_cast<click_icmp *>(p->data() + offset);
			tr_offset = sizeof(click_icmp);
			proto = IP_PROTO_ICMP;
		}
		// TCP
		else if ( ip->ip_p == IP_PROTO_TCP ) {
			hdr_pointer = reinterpret_cast<click_tcp *>(p->data() + offset);
			tr_offset = sizeof(click_tcp);
			proto = IP_PROTO_TCP;
		}
		// UDP
		else if ( ip->ip_p == IP_PROTO_UDP ) {
			hdr_pointer = reinterpret_cast<click_udp *>(p->data() + offset);
			tr_offset = sizeof(click_udp);
			proto = IP_PROTO_UDP;
		}
		else {
			//click_chatter("[%s] Accepts only ICMP, TCP, and UDP packets", class_name());
			p->kill();
			return 0;
		}
	}
	else if (version == 0x06) {
		// IPv6 header is
		const click_ip6 *ip6 = reinterpret_cast<const click_ip6 *>(p->data() + offset);
		offset += sizeof(click_ip6);

		// ICMP
		if ( ip6->ip6_nxt == 0x01 ) {
			hdr_pointer = reinterpret_cast<click_icmp *>(p->data() + offset);
			tr_offset = sizeof(click_icmp);
			proto = IP_PROTO_ICMP;
		}
		// TCP
		else if ( ip6->ip6_nxt == 0x06 ) {
			hdr_pointer = reinterpret_cast<click_tcp *>(p->data() + offset);
			tr_offset = sizeof(click_tcp);
			proto = IP_PROTO_TCP;
		}
		// UDP
		else if ( ip6->ip6_nxt == 0x11 ) {
			hdr_pointer = reinterpret_cast<click_udp *>(p->data() + offset);
			tr_offset = sizeof(click_udp);
			proto = IP_PROTO_UDP;
		}
		else { // the next header is not ICMP/TCP/UDP so stop now
			//click_chatter("[%s] Accepts only ICMP, TCP, and UDP packets", class_name());
			p->kill();
			return 0;
		}
	}
	else {
		click_chatter("Unknown IP version!");
		p->kill();
		return 0;
	}

	if ( p->length() < offset + tr_offset + sizeof(PData) ) {
		//click_chatter("Packet is too short");
		p->kill();
		return 0;
	}

	// Eth, IP, headers must be bypassed with a correct offset
	pData = (PData*)((char*)hdr_pointer + tr_offset);
	if      ( proto == IP_PROTO_ICMP )
		csum = reinterpret_cast<click_icmp *>(hdr_pointer)->icmp_cksum;
	else if ( proto == IP_PROTO_TCP  )
		csum = reinterpret_cast<click_tcp  *>(hdr_pointer)->th_sum;
	else if ( proto == IP_PROTO_UDP  )
		csum = reinterpret_cast<click_udp  *>(hdr_pointer)->uh_sum;

	// we use incremental checksum computation to patch up the checksum after
	// the payload will get modified
	_count++;
	if (_delta) {
		Timestamp ts1(ntohl(pData->data[0]), Timestamp::nsec_to_subsec(ntohl(pData->data[1])));
		Timestamp diff = Timestamp::now() - ts1;

		//click_chatter("Seq %d Time Diff sec: %d usec: %d\n", ntohl(pData->seq_num), diff.sec(), diff.nsec());
		pData->data[2] = htonl(diff.sec());
		pData->data[3] = htonl(diff.nsec());
		csum += click_in_cksum((const unsigned char*)(pData->data + 2), 2 * sizeof(uint32_t));
	}
	else {
		tnow = Timestamp::now();

		// subtract the previous contribution from CHECKSUM in case PData values are non-zero
		csum -= click_in_cksum((const unsigned char*)pData, sizeof(PData));
		pData->seq_num = htonl(_count);
		pData->data[0] = htonl(tnow.sec());
		pData->data[1] = htonl(tnow.nsec());
		pData->data[2] = 0;
		pData->data[3] = 0;
		csum += click_in_cksum((const unsigned char*)pData, sizeof(PData));
	}

	csum = (0xFFFF & csum) + ((0xFFFF0000 & csum) >> 16);
	csum = (csum != 0xFFFF) ? csum : 0;

	// now that we modified the packet it is time to fix up the csum of the header.
	if      ( proto == IP_PROTO_ICMP )
		reinterpret_cast<click_icmp *>(hdr_pointer)->icmp_cksum = (uint16_t)csum;
	else if ( proto == IP_PROTO_TCP  )
		reinterpret_cast<click_tcp  *>(hdr_pointer)->th_sum     = (uint16_t)csum;
	else if ( proto == IP_PROTO_UDP  )
		reinterpret_cast<click_udp  *>(hdr_pointer)->uh_sum     = (uint16_t)csum;

	return p;
}

#if HAVE_BATCH
PacketBatch*
StoreTimeSeqRecord::simple_action_batch(PacketBatch *batch)
{
	EXECUTE_FOR_EACH_PACKET_DROPPABLE(simple_action, batch, [](Packet *p){});
	return batch;
}
#endif

int
StoreTimeSeqRecord::reset_handler(const String &, Element *e, void *, ErrorHandler *) {
	StoreTimeSeqRecord *t = static_cast<StoreTimeSeqRecord *>(e);
	t->_count = 0;
	return 0;
}

void
StoreTimeSeqRecord::add_handlers() {
	add_data_handlers("count", Handler::f_read, &_count);
	add_write_handler("reset", reset_handler);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(StoreTimeSeqRecord)
