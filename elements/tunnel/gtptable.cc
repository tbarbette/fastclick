/*
 * GTPTable.{cc,hh}
 * Tom Barbette
 *
 * Copyright (c) 2018 University of Liege
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
#include <clicknet/gtp.h>
#include <clicknet/icmp.h>
#include <clicknet/udp.h>
#include <clicknet/ip.h>
#include <click/args.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include "gtptable.hh"

CLICK_DECLS

GTPTable::GTPTable() : _verbose(true)
{
}

GTPTable::~GTPTable()
{
}

int
GTPTable::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
            .read_mp("PING_DST",_ping_dst)
            .read("VERBOSE", _verbose)
	.complete() < 0)
	return -1;

    return 0;
}

int
GTPTable::process(int port, Packet* p) {
    click_jiffies_t now = click_jiffies();
    if (port == 0) {
        const click_ip *ip = reinterpret_cast<const click_ip *>(p->data());
        unsigned hlen = ip->ip_hl << 2;
        //const click_udp *udp = reinterpret_cast<const click_udp *>(p->data() + hlen);


        const click_gtp *gtp = reinterpret_cast<const click_gtp *>(p->data() + 28);

        GTPFlowID gtp_in = GTPFlowID(IPFlowID(p),ntohl(gtp->gtp_teid));
        int sz = sizeof(click_gtp);
        if (gtp->gtp_flags)
            sz += 4;
        sz+= hlen + sizeof(click_udp);
        bool known;
        {//Block to protect hashtable pointer scope
            GTPFlowTable::write_ptr gtp_out = _gtpmap.find_write(gtp_in);

            if (!gtp_out) {
                known = false;

                if (_verbose)
                    click_chatter("Unknown, emitting ping FOR TEID %u",gtp_in.gtp_id);

                uint32_t gen = click_random();
                uint16_t icmp_id = gen + 3;
                uint16_t icmp_seq = (gen >> 16) + 3;

                size_t data_size = 60;
                WritablePacket* q = Packet::make(sz + data_size);
                if (q) {
                    //Craft the ICMP PING
                    memcpy(q->data(), p->data(), sz);
                    click_ip* oip = (click_ip*)q->data();
                    oip->ip_len = ntohs(q->length());
                    oip->ip_sum = 0;
                    oip->ip_sum = click_in_cksum((unsigned char *)oip, hlen);
                    click_udp *oudph = (click_udp*)(q->data() + sizeof(click_ip));
                    int len = q->length() - sizeof(click_ip);
                    oudph->uh_ulen = htons(len);
                    oudph->uh_sum = 0;
                    //unsigned csum = click_in_cksum((unsigned char *)oudph, len);
                    //oudph->uh_sum = click_in_cksum_pseudohdr(csum, oip, len);

                    q->pull(sz);
                    memset(q->data(), '\0', data_size);

                    click_ip *nip = reinterpret_cast<click_ip *>(q->data());
                    nip->ip_v = 4;
                    nip->ip_hl = sizeof(click_ip) >> 2;
                    nip->ip_len = htons(q->length());
                    uint16_t ip_id = (gen % 0xFFFF) + 1; // ensure ip_id != 0
                    nip->ip_id = htons(ip_id);
                    nip->ip_p = IP_PROTO_ICMP; /* icmp */
                    nip->ip_ttl = 200;
                    nip->ip_src = IPAddress(((click_ip*)(p->data() + sz))->ip_src);
                    nip->ip_dst = _ping_dst;
                    nip->ip_sum = click_in_cksum((unsigned char *)nip, sizeof(click_ip));

                    click_icmp_echo *icp = (struct click_icmp_echo *) (nip + 1);
                    icp->icmp_type = ICMP_ECHO;
                    icp->icmp_code = 0;
                    icp->icmp_identifier = icmp_id;
                    icp->icmp_sequence = icmp_seq;

                    icp->icmp_cksum = click_in_cksum((const unsigned char *)icp, data_size - sizeof(click_ip));

                    q->set_dst_ip_anno(IPAddress(_ping_dst));
                    q->set_ip_header(nip, sizeof(click_ip));

                    ICMPFlowID icmpflowid(q);
                    if (_verbose)
                       click_chatter("Adding ICMP MAP %s %s %u %u FOR TEID %u",
                               icmpflowid.saddr().unparse().c_str(),
                               icmpflowid.daddr().unparse().c_str(),
                               icmpflowid.id(),
                               icmpflowid.seq(),gtp_in.gtp_id);

                    _icmp_map.find_insert(icmpflowid,gtp_in);

                    q = q->push(sz);

                    oip = (click_ip*)q->data();

                    if (in_batch_mode)
                        checked_output_push_batch(1,PacketBatch::make_from_packet(q));
                    else
                        output_push(1,q);
                }
            } else { //Else flow is known and has a mapping
                gtp_out->last_seen = now;

                gtp_out.release();
                if (_verbose)
                    click_chatter("Already seen GTP!");
                known = true;
            }


        }

        (void)known; //TODO

        //Pull packet to inner header
        p->pull(sz);

        const click_ip *innerip = reinterpret_cast<const click_ip *>(p->data());
        unsigned innerhlen = innerip->ip_hl << 2;
        p->set_ip_header((click_ip*)p->data(), innerhlen);
        IPFlowID inner(p,true);
        if (innerip->ip_p != 17 && innerip->ip_p != 6) {
            inner.set_dport(0);
            inner.set_sport(0);
        }
        //Now add a reverse mapping to the REV 4 tupple -> GTP IN or OUT if known
        {
	    if (_verbose) {
	            click_chatter("Setting INNER mapping for TEID %u",gtp_in.gtp_id);
	            click_chatter("%s",inner.unparse().c_str());
	    }
            INMap::ptr gtp_ptr = _inmap.find_insert(inner,GTPFlowIDMAP(gtp_in));
            gtp_ptr->last_seen = now;
            /*if (known && !gtp_ptr->known) {
                if (_verbose)
                    click_chatter("Inner mapping is now known !");
                *gtp_ptr = *_gtpmap.find(gtp_in);
                gtp_ptr->known = true; //Inner mapping is now mapping the OUT directly !
            }*/
            gtp_ptr.release();
        }

        return 0;
    } else {
        const click_gtp *gtp = reinterpret_cast<const click_gtp *>(p->data() + 28);
        GTPFlowID gtp_out = GTPFlowID(IPFlowID(p,false),ntohl(gtp->gtp_teid));
        if (_verbose)
            click_chatter("PING RECEIVED, TEID %u",gtp_out.gtp_id);
        GTPFlowID gtp_in;

        const click_ip* innerip = reinterpret_cast<const click_ip*>(gtp + 1);
        ICMPFlowID icmpflowid(innerip,true);
        if (_verbose)
           click_chatter("Searching ICMP MAP %s %s %u %u FOR TEID OUT %u",
                   icmpflowid.saddr().unparse().c_str(),
                   icmpflowid.daddr().unparse().c_str(),
                   icmpflowid.id(),
                   icmpflowid.seq(),gtp_out.gtp_id);
        if (!_icmp_map.find_remove(icmpflowid,gtp_in)) {
            click_chatter("Unknown FLOW ! Dropping packet");
            return -1;
        }
	if (_verbose) {
	        click_chatter("PING RECEIVED, IN TEID %u",gtp_in.gtp_id);
	        click_chatter("PING RECEIVED, ADDING MAP TEID %u->%u",gtp_in.gtp_id, gtp_out.gtp_id);
	}

        _gtpmap.set(gtp_in,gtp_out);

        assert(*_gtpmap.find(gtp_in) == gtp_out);

        //Delete the packet
        return -1;
    }
}

void
GTPTable::push(int port, Packet *p)
{
    int o = process(port,p);
    if (o == 3) //If 3, we just do nothing (packet is waiting in list)
        return;
    checked_output_push(o, p);
}

#if HAVE_BATCH
void
GTPTable::push_batch(int port, PacketBatch* batch) {
    auto fnt = [this,port](Packet*p) {
        return process(port,p);
    };
	CLASSIFY_EACH_PACKET(4,fnt,batch,[this](int o, PacketBatch*batch){if (o == 3) return;checked_output_push_batch(o,batch);});
	return;
}
#endif


CLICK_ENDDECLS
EXPORT_ELEMENT(GTPTable)
