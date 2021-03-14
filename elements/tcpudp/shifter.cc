/*
 * shifter.{cc,hh} -- shift packets' 4-tuple fields by a given offset
 * Massimo Girondi
 *
 * Copyright (c) 2021 KTH Royal Institute of Technology
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

#include "shifter.hh"
#include <click/config.h>
#include <click/args.hh>
#include <click/packet.hh>
#include <clicknet/ip.h>
#include <click/error.hh>



CLICK_DECLS

Shifter::Shifter() {}
Shifter::~Shifter() {}

int Shifter::configure(Vector<String> &conf, ErrorHandler *errh) {
    if (Args(conf, this, errh)
            .read_or_set_p("IP_OFFSET", _ipoffset, 0)
            .read_or_set_p("PORT_OFFSET", _portoffset, 0)
            .read_or_set_p("IP_OFFSET_DST", _ipoffset_dst, 0)
            .read_or_set_p("PORT_OFFSET_DST", _portoffset_dst, 0)

            .complete() < 0)
        return -1;

    return 0;
}

inline Packet* Shifter::process(Packet *p) {
    WritablePacket *q = p->uniqueify();
    uint8_t ip_proto;
    uint16_t sport, dport;
    uint32_t ip_src, ip_dst;
    p = q;
    ip_proto = p->ip_header()->ip_p;

    // Process only TCP and UDP packets
    if (ip_proto == IP_PROTO_TCP || ip_proto == IP_PROTO_UDP) {
        uint32_t ip_src = ntohl(p->ip_header()->ip_src.s_addr);
        uint16_t sport = ntohs(p->tcp_header()->th_sport);

        sport = ((uint64_t)sport) + _portoffset;
        ip_src = ((uint64_t)ip_src) + _ipoffset;

        uint32_t ip_dst = ntohl(p->ip_header()->ip_dst.s_addr);
        uint16_t dport = ntohs(p->tcp_header()->th_dport);
        dport = ((uint64_t)dport) + _portoffset_dst;
        ip_dst = ((uint64_t)ip_dst) + _ipoffset_dst;

        q->rewrite_ipport(htonl(ip_src), htons(sport), 0, ip_proto == IP_PROTO_TCP);
        q->rewrite_ipport(htonl(ip_dst), htons(dport), 1, ip_proto == IP_PROTO_TCP);
    }
    return q;
}

#if HAVE_BATCH
void Shifter::push_batch(int, PacketBatch *batch) {
    EXECUTE_FOR_EACH_PACKET(process,batch);
    output_push_batch(0, batch);
}
#endif

Packet *Shifter::simple_action(Packet *p) {
    p = process(p);
    return p;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(Shifter)
ELEMENT_MT_SAFE(Shifter)
