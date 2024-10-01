/*
 * ip6srdecap.{cc,hh} -- element encapsulates packet in IP6 SRv6 header
 * Tom Barbette, Louis Navarre
 *
 * Copyright (c) 2024 UCLouvain
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
#include "ip6srdecap.hh"
#include <click/nameinfo.hh>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/glue.hh>
CLICK_DECLS

IP6SRDecap::IP6SRDecap() : _force(false)
{

}

IP6SRDecap::~IP6SRDecap()
{
}

int
IP6SRDecap::configure(Vector<String> &conf, ErrorHandler *errh)
{

    if (Args(conf, this, errh)
        .read("FORCE_DECAP", _force)
        .complete() < 0)
        return -1;
    
    return 0;
}


Packet *
IP6SRDecap::simple_action(Packet *p_in)
{
    
    WritablePacket *p = p_in->uniqueify();
    if (!p)
        return 0;

    click_ip6 *ip6 = reinterpret_cast<click_ip6 *>(p->data());
    click_ip6_sr *sr = (click_ip6_sr*)ip6_find_header(ip6, IP6_EH_ROUTING, p->end_data());

    if (sr == 0) {
        return p;
    }
    
    if (_force || sr->segment_left == 0) {
            IP6Address last = IP6Address(sr->segments[0]);

            unsigned char* old_data = p->data();
            unsigned char nxt = sr->ip6_sr_next;
            unsigned char *next_ptr = (unsigned char*)ip6_find_header(ip6, nxt, p->end_data());
            unsigned offset = p->transport_header_offset();
            if (next_ptr == 0) {
                p->kill();
                click_chatter("Cannot find next header %d. Buggy packet?", nxt);
                return 0;
            }
            unsigned srlen = (unsigned char*)next_ptr - (unsigned char*)sr;

            if (srlen > 64 && click_current_cpu_id() == 13) {
                char buf[5000];
                char* b = buf;
                for (int i = 0; i < p->length(); i++) {
                        if (i%4==0)
                            b += sprintf(b, " ");
                        if (i%16==0)
                            b += sprintf(b, "\n[%d]", click_current_cpu_id());
                        b += sprintf(b, "%02x", p->data()[i] & 0xff);
                }
                *b = '\0';
                click_chatter("[%d] Culprit [%d]: %s / %p %p",click_current_cpu_id(), p->length(), buf, p->data(), ip6);
                click_chatter("[%d] srlen %d, [nxt] %d, nxt %x", click_current_cpu_id(), srlen, nxt, ip6->ip6_nxt);
                click_chatter("[%d] Sr length too big, sr at offset %d", click_current_cpu_id(), (char*)sr-(char*)ip6);
                auto fnt = [p] (const uint8_t type, unsigned char* hdr) __attribute__((always_inline)) {
                    click_chatter("[%d] NXT %d at offset %d: %x",click_current_cpu_id(), type, hdr - p->data(), *hdr);
                    return true;
                };
                ip6_follow_eh<decltype(fnt)>(ip6, (unsigned char*)p->end_data(), fnt);

                b = buf;
                for (int i =0; i < p->length(); i++) {
                        b += sprintf(b, "%02x", p->data()[i] & 0xff);
                }
                *b = '\0';
                click_chatter("[%d] Culprit [%d]: %s",click_current_cpu_id(),p->length(), buf);
                assert(false);
            }

            p->pull(srlen);

            memmove(p->data(), old_data, (unsigned char*)sr-old_data);
            ip6 = (click_ip6 *)(p->data());

            ip6->ip6_dst = last;
            ip6->ip6_nxt = nxt;
            ip6->ip6_plen = htons(ntohs(ip6->ip6_plen) - srlen);
            // p->set_network_header(p->data(), offset - srlen);
            p->set_network_header(p->data(), 40);


    } else if (unlikely(sr->segment_left == 0)) {
        click_chatter("Invalid packet with 0 segments left?");
        return p;
    } else {
        ip6->ip6_dst = sr->segments[--sr->segment_left];
    }

    return p;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IP6SRDecap)
ELEMENT_MT_SAFE(IP6SRDecap)
