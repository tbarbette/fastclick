/*
 * timestampdiff.{cc,hh} -- Store a packet count inside packet payload
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

#include "timestampdiff.hh"

#include <climits>
#include <cmath>

#include <click/args.hh>
#include <click/error.hh>
#include <click/straccum.hh>
#include <click/timestamp.hh>

#include "numberpacket.hh"
#include "recordtimestamp.hh"

CLICK_DECLS

TimestampDiff::TimestampDiff() : _delays(), _offset(40) {
}

TimestampDiff::~TimestampDiff() {
}

int TimestampDiff::configure(Vector<String> &conf, ErrorHandler *errh) {
    if (Args(conf, this, errh)
            .read("OFFSET",_offset)
            .complete() < 0)
        return -1;

    return 0;
}

enum {
    TSD_AVG_HANDLER,
    TSD_MIN_HANDLER,
    TSD_MAX_HANDLER,
    TSD_STD_HANDLER,
    TSD_DUMP_HANDLER
};

String TimestampDiff::read_handler(Element *e, void *arg) {
    TimestampDiff *tsd = static_cast<TimestampDiff *>(e);
    unsigned min = UINT_MAX;
    unsigned max = 0;
    double sum = 0.0;
    for (auto delay : tsd->_delays) {
        sum += static_cast<double>(delay);
        if (delay < min)
            min = delay;
        if (delay > max)
            max = delay;
    }
    double mean = sum / tsd->_delays.size();
    switch (reinterpret_cast<intptr_t>(arg)) {
        case TSD_AVG_HANDLER:
            return String(mean);
        case TSD_MIN_HANDLER:
            return String(min);
        case TSD_MAX_HANDLER:
            return String(max);
        case TSD_STD_HANDLER: {
            double var = 0.0;
            for (auto delay : tsd->_delays)
                var += pow(delay - mean, 2);
            return String(sqrt(var / tsd->_delays.size()));
        }
        case TSD_DUMP_HANDLER: {
            StringAccum s;
            for (size_t i = 0; i < tsd->_delays.size(); ++i)
                s << i << ": " << String(tsd->_delays[i]) << "\n";
            return s.take_string();
        }
        default:
            return String("Unknown read handler for TimestampDiff");
    }
}

void TimestampDiff::add_handlers() {
    add_read_handler("average", read_handler, TSD_AVG_HANDLER);
    add_read_handler("min", read_handler, TSD_MIN_HANDLER);
    add_read_handler("max", read_handler, TSD_MAX_HANDLER);
    add_read_handler("stddev", read_handler, TSD_STD_HANDLER);
    add_read_handler("dump", read_handler, TSD_DUMP_HANDLER);
}

inline void TimestampDiff::smaction(Packet* p) {
    Timestamp now = Timestamp::now_steady();
    uint64_t i = NumberPacket::read_number_of_packet(p,_offset);
    Timestamp old = get_recordtimestamp_instance()->get(i);
    Timestamp diff = now - old;
    if (diff.sec() > 0)
        click_chatter("delay over 1s for packet %llu: %uµs",
                      i, diff.sec() * 1000000 + diff.usec());
    else
        _delays.push_back(diff.usec());
}

void TimestampDiff::push(int, Packet *p) {
    smaction(p);
    output(0).push(p);
}

#if HAVE_BATCH
void
TimestampDiff::push_batch(int, PacketBatch * batch) {
    FOR_EACH_PACKET(batch,p)
            smaction(p);
    output(0).push_batch(batch);
}
#endif

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel)
EXPORT_ELEMENT(TimestampDiff)
