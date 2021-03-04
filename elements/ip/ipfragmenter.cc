// -*- c-basic-offset: 4 -*-
/*
 * ipfragmenter.{cc,hh} -- element fragments IP packets
 * Robert Morris, Eddie Kohler
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2002 International Computer Science Institute
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
#include "ipfragmenter.hh"
#include <clicknet/ip.h>
#include <click/args.hh>
#include <click/error.hh>
#include <click/glue.hh>
CLICK_DECLS

IPFragmenter::IPFragmenter()
    : _honor_df(true), _verbose(false), _mtu(0)
{
    _fragments = 0;
    _drops = 0;
}

IPFragmenter::~IPFragmenter()
{
}


int
IPFragmenter::configure(Vector<String> &conf, ErrorHandler *errh)
{
    _headroom = Packet::default_headroom;
    if (Args(conf, this, errh)
	.read_mp("MTU", _mtu)
	.read_p("HONOR_DF", _honor_df)
	.read_p("VERBOSE", _verbose)
	.read("HEADROOM", _headroom)
	.complete() < 0)
	return -1;
    if (_mtu < 8)
	return errh->error("MTU must be at least 8");
    return 0;
}

int
IPFragmenter::optcopy(const click_ip *ip1, click_ip *ip2)
{
    const uint8_t* oin = (const uint8_t*) (ip1 + 1);
    const uint8_t* oin_end = oin + (ip1->ip_hl << 2) - sizeof(click_ip);
    uint8_t *oout = (uint8_t *) (ip2 + 1);
    int outpos = 0;

    while (oin < oin_end)
	if (*oin == IPOPT_NOP)  // don't copy NOP
	    ++oin;
	else if (*oin == IPOPT_EOL
                 || oin + 1 == oin_end
                 || oin[1] < 2
		 || oin + oin[1] > oin_end)
	    break;
        else {
            if (*oin & 0x80) {	// copy the option
                if (ip2)
                    memcpy(oout + outpos, oin, oin[1]);
                outpos += oin[1];
            }
            oin += oin[1];
        }

    for (; (outpos & 3) != 0; outpos++)
	if (ip2)
	    oout[outpos] = IPOPT_EOL;

    return outpos;
}

void
IPFragmenter::fragment(Packet *p_in)
{
    const click_ip *ip_in = p_in->ip_header();
    int hlen = ip_in->ip_hl << 2;
    int first_dlen = (_mtu - hlen) & ~7;
    int in_dlen = ntohs(ip_in->ip_len) - hlen;

    if (((ip_in->ip_off & htons(IP_DF)) && _honor_df) || first_dlen < 8) {
	if (_verbose || _drops < 5)
	    click_chatter("IPFragmenter(%d) DF %p{ip_ptr} %p{ip_ptr} len=%d", _mtu, &ip_in->ip_src, &ip_in->ip_dst, p_in->length());
	_drops++;
	    if (receives_batch)
	        checked_output_push_batch(1, PacketBatch::make_from_packet(p_in));
	    else
	        checked_output_push(1, p_in);
	return;
    }

    // make sure we can modify the packet
    WritablePacket *p = p_in->uniqueify();
    if (!p)
	return;
    click_ip *ip = p->ip_header();

    // output the first fragment
    // If we're cheating the DF bit, we can't trust the ip_id; set to random.
    if (ip->ip_off & htons(IP_DF)) {
	ip->ip_id = click_random();
	ip->ip_off &= ~htons(IP_DF);
    }
    bool had_mf = (ip->ip_off & htons(IP_MF)) != 0;
    ip->ip_len = htons(hlen + first_dlen);
    ip->ip_off |= htons(IP_MF);
    ip->ip_sum = 0;
    ip->ip_sum = click_in_cksum((const unsigned char *)ip, hlen);
    Packet *first_fragment = p->clone();
    first_fragment->take(p->length() - p->network_header_offset() - hlen - first_dlen);
#if HAVE_BATCH
    if (receives_batch)
        output_push_batch(0, PacketBatch::make_from_packet(first_fragment));
    else
#endif
        output(0).push(first_fragment);
    _fragments++;

    // output the remaining fragments
    int out_hlen = sizeof(click_ip) + optcopy(ip, 0);

    for (int off = first_dlen; off < in_dlen; ) {
	// prepare packet
	int out_dlen = (_mtu - out_hlen) & ~7;
	if (out_dlen + off > in_dlen)
	    out_dlen = in_dlen - off;

	WritablePacket *q = Packet::make(_headroom, 0, out_hlen + out_dlen, 0);
	if (q) {
	    q->set_network_header(q->data(), out_hlen);
	    click_ip *qip = q->ip_header();

	    memcpy(qip, ip, sizeof(click_ip));
	    optcopy(ip, qip);
	    memcpy(q->transport_header(), p->transport_header() + off, out_dlen);

	    qip->ip_hl = out_hlen >> 2;
	    qip->ip_off = htons(ntohs(ip->ip_off) + (off >> 3));
	    if (out_dlen + off >= in_dlen && !had_mf)
		qip->ip_off &= ~htons(IP_MF);
	    qip->ip_len = htons(out_hlen + out_dlen);
	    qip->ip_sum = 0;
	    qip->ip_sum = click_in_cksum((const unsigned char *)qip, out_hlen);

	    q->copy_annotations(p);
#if HAVE_BATCH
	    if (receives_batch)
	        output_push_batch(0, PacketBatch::make_from_packet(q));
	    else
#endif
	        output(0).push(q);
	    _fragments++;
	}

	off += out_dlen;
    }

    p->kill();
}

void
IPFragmenter::push(int, Packet *p)
{
    if (p->network_length() <= (int) _mtu)
	output(0).push(p);
    else
	fragment(p);
}

#if HAVE_BATCH
void
IPFragmenter::push_batch(int, PacketBatch *batch)
{
    EXECUTE_FOR_EACH_PACKET_SPLITTABLE([this](Packet*p){return (p->network_length() <= (int) _mtu)?p:0;}, //Return p if _mtu is ok, 0 if not
                                        batch, //The batch
                                        fragment, //Call fragment on the packet that is not _mtu sized
                                        [this](PacketBatch*batch){output_push_batch(0,batch);}); //Flush the batch before calling fragment
}
#endif

void
IPFragmenter::add_handlers()
{
    add_data_handlers("drops", Handler::OP_READ, &_drops);
    add_data_handlers("fragments", Handler::OP_READ, &_fragments);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IPFragmenter)
ELEMENT_MT_SAFE(IPFragmenter)
