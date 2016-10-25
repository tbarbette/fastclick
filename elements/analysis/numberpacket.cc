/*
 * numberpacket.{cc,hh} -- Store a packet count inside packet payload
 * Cyril Soldani, Tom Barbette
 *
 * Copyright (c) 2015-2016 University of Liège
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

#include "numberpacket.hh"

#include <click/args.hh>
#include <click/error.hh>

CLICK_DECLS

NumberPacket::NumberPacket() : _count(0) {
}

NumberPacket::~NumberPacket() {
}

int NumberPacket::configure(Vector<String> &conf, ErrorHandler *errh) {
    if (Args(conf, this, errh).complete() < 0)
        return -1;

    return 0;
}

inline Packet* NumberPacket::smaction(Packet* p) {
    WritablePacket *wp = nullptr;
    if (p->length() >= HEADER_SIZE + 8)
        wp = p->uniqueify();
    else {
        wp = p->put(HEADER_SIZE + 8 - p->length());
        assert(wp);
        wp->ip_header()->ip_len = htons(HEADER_SIZE + 8);
    }
    // Skip header
    *reinterpret_cast<uint64_t *>(wp->data() + HEADER_SIZE) = _count++;

    return wp;
}

void NumberPacket::push(int, Packet *p) {
    output(0).push(smaction(p));
}

#if HAVE_BATCH
void NumberPacket::push_batch(int, PacketBatch *batch) {
    FOR_EACH_PACKET_SAFE(batch,p) {
        p = smaction(p);
    }
    output(0).push_batch(batch);
}
#endif

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel)
EXPORT_ELEMENT(NumberPacket)
