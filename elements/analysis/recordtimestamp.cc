/*
 * recordtimestamp.{cc,hh} -- Store a packet count inside packet payload
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

#include <click/config.h> // Doc says this should come first

#include "recordtimestamp.hh"
#include "numberpacket.hh"

#include <click/args.hh>
#include <click/error.hh>

CLICK_DECLS

RecordTimestamp::RecordTimestamp() : _offset(-1), _timestamps() {
}

RecordTimestamp::~RecordTimestamp() {
}

int RecordTimestamp::configure(Vector<String> &conf, ErrorHandler *errh) {
    unsigned n = 0;
    if (Args(conf, this, errh)
            .read("N", n)
            .read("OFFSET", _offset)
            .complete() < 0)
        return -1;

    if (n == 0)
        n = 65536;
    _timestamps.reserve(n);

    return 0;
}

inline void
RecordTimestamp::smaction(Packet* p) {
    uint64_t i;
    if (_offset >= 0) {
        i = NumberPacket::read_number_of_packet(p, _offset);
        assert(i < INT_MAX);
        while (i >= _timestamps.size()) {
            _timestamps.resize(_timestamps.size() == 0? _timestamps.capacity():_timestamps.size() * 2, Timestamp::uninitialized_t());
        }
        _timestamps.unchecked_at(i) = Timestamp::now_steady();
    } else {
        _timestamps.push_back(Timestamp::now_steady());
    }

}

void RecordTimestamp::push(int, Packet *p) {
    smaction(p);
    output(0).push(p);
}

#if HAVE_BATCH
void RecordTimestamp::push_batch(int, PacketBatch *batch) {
    FOR_EACH_PACKET(batch, p) {
        smaction(p);
    }
    output(0).push_batch(batch);
}
#endif

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel)
EXPORT_ELEMENT(RecordTimestamp)
