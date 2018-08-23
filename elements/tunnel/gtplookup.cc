/*
 * GTPLookup.{cc,hh}
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
#include "gtplookup.hh"
#include "gtptable.hh"

CLICK_DECLS

GTPLookup::GTPLookup() : _checksum(true)
{
}

GTPLookup::~GTPLookup()
{
}

int
GTPLookup::configure(Vector<String> &conf, ErrorHandler *errh)
{
    Element* e;
    if (Args(conf, this, errh)
            .read_mp("TABLE",e)
            .read("CHECKSUM", _checksum)
	.complete() < 0)
	return -1;

    if (!e || !e->cast("GTPTable"))
        return errh->error("Unknown GTPTable");
    _table = static_cast<GTPTable*>(e);

    return 0;
}

bool
GTPLookup::run_task(Task* t) {
	Packet* batch = _queue.get();
	Packet* p = batch;
	if (p == 0)
		return false;
	Packet* next;
	do {
		next = p->next();
	} while ((p = next) != 0);
    return true;
}

int
GTPLookup::process(int port, Packet* p_in) {
    //click_jiffies_t now = click_jiffies();
    IPFlowID inner(p_in);
    if (p_in->ip_header()->ip_p != 17 && p_in->ip_header()->ip_p != 6) {
        inner.set_dport(0);
        inner.set_sport(0);
    }
    auto gtp_tunnel = _table->_inmap.find(inner);
    if (!gtp_tunnel) {
        click_chatter("UNKNOWN PACKETS FROM TOF !!?");
        click_chatter("%s",inner.unparse().c_str());
        return -1;
    } else {
        //gtp_tunnel->last_seen = now;
        if (likely(gtp_tunnel->known)) { //This is the GTP_OUT directly

        } else { //This is the GTP_IN, we must resolve and update
            auto gtp_out = _table->_gtpmap.find(*gtp_tunnel);
            if (!gtp_out) {
                click_chatter("Mapping is still unknown ! Queuing packets. Choose a closer ping server...");

                return 2;
            }
            *gtp_tunnel = *gtp_out;
            gtp_tunnel->known = true;
        }
        WritablePacket *p = p_in->push(sizeof(click_gtp) + sizeof(click_udp) + sizeof(click_ip));

        click_gtp *gtp = reinterpret_cast<click_gtp *>(p->data() + sizeof(click_udp) + sizeof(click_ip));
        gtp->gtp_v = 1;
        gtp->gtp_pt = 1;
        gtp->gtp_reserved = 0;
        gtp->gtp_flags = 0;
        gtp->gtp_msg_type = 0xff;
        gtp->gtp_msg_len = htons(p->length() - sizeof(click_gtp));
        gtp->gtp_teid = htonl(gtp_tunnel->gtp_id);

        click_ip *ip = reinterpret_cast<click_ip *>(p->data());
        click_udp *udp = reinterpret_cast<click_udp *>(ip + 1);

          // set up IP header
          ip->ip_v = 4;
          ip->ip_hl = sizeof(click_ip) >> 2;
          ip->ip_len = htons(p->length());
          ip->ip_id = htons(_id.fetch_and_add(1));
          ip->ip_p = IP_PROTO_UDP;
          ip->ip_src = gtp_tunnel->ip_id.saddr();

          ip->ip_dst = gtp_tunnel->ip_id.daddr();
          p->set_dst_ip_anno(gtp_tunnel->ip_id.daddr());

          ip->ip_tos = 0;
          ip->ip_off = 0;
          ip->ip_ttl = 250;

          ip->ip_sum = 0;
        #if HAVE_FAST_CHECKSUM && FAST_CHECKSUM_ALIGNED
          if (_aligned)
            ip->ip_sum = ip_fast_csum((unsigned char *)ip, sizeof(click_ip) >> 2);
          else
            ip->ip_sum = click_in_cksum((unsigned char *)ip, sizeof(click_ip));
        #elif HAVE_FAST_CHECKSUM
          ip->ip_sum = ip_fast_csum((unsigned char *)ip, sizeof(click_ip) >> 2);
        #else
          ip->ip_sum = click_in_cksum((unsigned char *)ip, sizeof(click_ip));
        #endif

          p->set_ip_header(ip, sizeof(click_ip));

          // set up UDP header
          udp->uh_sport = gtp_tunnel->ip_id.sport();
          udp->uh_dport = gtp_tunnel->ip_id.dport();
          uint16_t len = p->length() - sizeof(click_ip);
          udp->uh_ulen = htons(len);
          udp->uh_sum = 0;
          if (_checksum) {
              unsigned csum = click_in_cksum((unsigned char *)udp, len);
              udp->uh_sum = click_in_cksum_pseudohdr(csum, ip, len);
          } else {
              udp->uh_sum = 0;
          }
          return 0;
    }
}


void
GTPLookup::push(int port, Packet *p)
{
    int o = process(port,p);
    if (o == 2) {
	    p->set_next(_queue.get());
	    _queue.set(p);
    } else
	    checked_output_push(o, p);
}

#if HAVE_BATCH
void
GTPLookup::push_batch(int port, PacketBatch* batch) {
    auto fnt = [this,port](Packet*p) {
        return process(port,p);
    };
	CLASSIFY_EACH_PACKET(3,fnt,batch,[this](int o, PacketBatch* batch){
			if (o == 2) {
			    batch->tail()->set_next(_queue.get());
			    _queue.set(batch);
			} else {
				checked_output_push_batch(o,batch);
			}
	});
	return;
}
#endif


CLICK_ENDDECLS
EXPORT_ELEMENT(GTPLookup)
