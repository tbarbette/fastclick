/*
 * recordtimestamp.{cc,hh} -- Store a timestamp for numbered packets
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

#include <click/args.hh>
#include <click/error.hh>

CLICK_DECLS

RecordTimestamp::RecordTimestamp() :
    _offset(-1), _dynamic(false), _net_order(false), _timestamps(), _np(0) {
}

RecordTimestamp::~RecordTimestamp() {
}

int RecordTimestamp::configure(Vector<String> &conf, ErrorHandler *errh) {
    uint32_t n = 0;
    Element *e = NULL;
    if (Args(conf, this, errh)
            .read("COUNTER", e)
            .read("N", n)
            .read("OFFSET", _offset)
            .read("DYNAMIC", _dynamic)
            .read("NET_ORDER", _net_order)
            .complete() < 0)
        return -1;

    if (n == 0)
        n = 65536;
    _timestamps.reserve(n);

    if (e && (_np = static_cast<NumberPacket *>(e->cast("NumberPacket"))) == 0)
        return errh->error("COUNTER must be a valid NumberPacket element");

    // Adhere to the settings of the counter element, bypassing the configuration
    if (_np) {
        _net_order = _np->has_net_order();
    }

    return 0;
}

inline void
RecordTimestamp::rmaction(Packet *p) {
    uint64_t i;
    if (_offset >= 0) {
        i = get_numberpacket(p, _offset, _net_order);
        assert(i < ULLONG_MAX);
        while (i >= (unsigned)_timestamps.size()) {
            if (!_dynamic && i >= (unsigned)_timestamps.capacity()) {
                click_chatter("Fatal error: DYNAMIC is not set and record timestamp reserved capacity is too small. Use N to augment the capacity.");
                assert(false);
            }
            _timestamps.resize(_timestamps.size() == 0? _timestamps.capacity():_timestamps.size() * 2, Timestamp::uninitialized_t());
        }
        _timestamps.unchecked_at(i) = Timestamp::now_steady();
    } else {
        _timestamps.push_back(Timestamp::now_steady());
    }
}

void RecordTimestamp::push(int, Packet *p) {
    rmaction(p);
    output(0).push(p);
}

#if HAVE_BATCH
void RecordTimestamp::push_batch(int, PacketBatch *batch) {
    FOR_EACH_PACKET(batch, p) {
        rmaction(p);
    }
    output(0).push_batch(batch);
}
#endif

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel)
EXPORT_ELEMENT(RecordTimestamp)
