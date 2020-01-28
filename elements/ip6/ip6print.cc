/*
 * ip6print.{cc,hh} -- dumps simple ip6 information to screen
 * Benjie Chen
 *
 * Computational batching support and active parameter with handler
 * by Georgios Katsikas
 *
 * Copyright (c) 2020 UBITECH and KTH Royal Institute of Technology
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
#include "ip6print.hh"
#include <click/glue.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/straccum.hh>
#include <click/packet_anno.hh>
#include <click/ip6address.hh>
#if CLICK_USERLEVEL
# include <stdio.h>
#endif

CLICK_DECLS

IP6Print::IP6Print() :
    _label(), _bytes(1500), _contents(false), _active(true)
{
}

IP6Print::~IP6Print()
{
}

int
IP6Print::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
        .read_p("LABEL", _label)
        .read("NBYTES", _bytes)
        .read("CONTENTS", _contents)
        .read("ACTIVE", _active)
        .complete() < 0)
        return -1;
    return 0;
}

Packet *
IP6Print::simple_action(Packet *p)
{
    if (!_active) {
        return p;
    }

    const click_ip6 *iph = (click_ip6*) p->ip_header();

    StringAccum sa;
    if (_label)
        sa << _label << ": ";

    sa << reinterpret_cast<const IP6Address &>(iph->ip6_src)
       << " -> "
       << reinterpret_cast<const IP6Address &>(iph->ip6_dst)
       << " plen " << ntohs(iph->ip6_plen)
       << ", next " << (int)iph->ip6_nxt
       << ", hlim " << (int)iph->ip6_hlim << "\n";

    const unsigned char *data = p->data();
    if (_contents) {
        int amt = 3*_bytes + (_bytes/4+1) + 3*(_bytes/24+1) + 1;

        sa << "  ";
        char *buf = sa.reserve(amt);
        char *orig_buf = buf;

        if (buf) {
            for (unsigned i = 0; i < _bytes && i < p->length(); i++) {
                sprintf(buf, "%02x", data[i] & 0xff);
                buf += 2;
                if ((i % 24) == 23) {
                    *buf++ = '\n'; *buf++ = ' '; *buf++ = ' ';
                } else if ((i % 4) == 3)
                    *buf++ = ' ';
            }
        }
        if (orig_buf) {
            assert(buf <= orig_buf + amt);
            sa.adjust_length(buf - orig_buf);
        }
    }
    click_chatter("%s", sa.c_str());
    return p;
}

void
IP6Print::add_handlers()
{
    add_data_handlers("active", Handler::OP_READ | Handler::OP_WRITE | Handler::CHECKBOX | Handler::CALM, &_active);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IP6Print)
