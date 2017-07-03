/*
 * timestampdiff.{cc,hh} -- Store a packet count inside packet payload
 * Cyril Soldani, Tom Barbette
 *
 * Various latency percentiles by Georgios Katsikas
 *
 * Copyright (c) 2015-2016 University of Liège
 * Copyright (c) 2017 RISE SICS AB
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
#include <algorithm>

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

    Element* e;
    if (Args(conf, this, errh)
            .read_mp("RECORDER", e)
            .read("OFFSET",_offset)
            .complete() < 0)
        return -1;

    if ((_rt = static_cast<RecordTimestamp*>(e->cast("RecordTimestamp"))) == 0)
        return errh->error("RECORDER must be a valid RecordTimestamp element");

    return 0;
}

enum {
    TSD_AVG_HANDLER,
    TSD_MIN_HANDLER,
    TSD_MAX_HANDLER,
    TSD_STD_HANDLER,
    TSD_PERC_00_HANDLER,
    TSD_PERC_01_HANDLER,
    TSD_PERC_05_HANDLER,
    TSD_PERC_10_HANDLER,
    TSD_PERC_25_HANDLER,
    TSD_MED_HANDLER,
    TSD_PERC_75_HANDLER,
    TSD_PERC_90_HANDLER,
    TSD_PERC_95_HANDLER,
    TSD_PERC_99_HANDLER,
    TSD_PERC_100_HANDLER,
    TSD_DUMP_HANDLER
};

String TimestampDiff::read_handler(Element *e, void *arg) {
    TimestampDiff *tsd = static_cast<TimestampDiff *>(e);
    unsigned min = UINT_MAX;
    double  mean = 0.0;
    unsigned max = 0;

    // Return updated min, mean, and max values
    tsd->min_mean_max(tsd->_delays, min, mean, max);

    switch (reinterpret_cast<intptr_t>(arg)) {
        case TSD_MIN_HANDLER:
            return String(min);
        case TSD_AVG_HANDLER:
            return String(mean);
        case TSD_MAX_HANDLER:
            return String(max);
        case TSD_STD_HANDLER: {
            double var = 0.0;
            for (auto delay : tsd->_delays)
                var += pow(delay - mean, 2);
            return String(sqrt(var / tsd->_delays.size()));
        }
        case TSD_PERC_00_HANDLER:
            return String(min);
        case TSD_PERC_01_HANDLER:
            return String(tsd->percentile(tsd->_delays, 1));
        case TSD_PERC_05_HANDLER:
            return String(tsd->percentile(tsd->_delays, 5));
        case TSD_PERC_10_HANDLER:
            return String(tsd->percentile(tsd->_delays, 10));
        case TSD_PERC_25_HANDLER:
            return String(tsd->percentile(tsd->_delays, 25));
        case TSD_MED_HANDLER:
            return String(tsd->percentile(tsd->_delays, 50));
        case TSD_PERC_75_HANDLER:
            return String(tsd->percentile(tsd->_delays, 75));
        case TSD_PERC_90_HANDLER:
            return String(tsd->percentile(tsd->_delays, 90));
        case TSD_PERC_95_HANDLER:
            return String(tsd->percentile(tsd->_delays, 95));
        case TSD_PERC_99_HANDLER:
            return String(tsd->percentile(tsd->_delays, 99));
        case TSD_PERC_100_HANDLER:
            return String(max);
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
    add_read_handler("perc00", read_handler, TSD_PERC_00_HANDLER);
    add_read_handler("perc01", read_handler, TSD_PERC_01_HANDLER);
    add_read_handler("perc05", read_handler, TSD_PERC_05_HANDLER);
    add_read_handler("perc10", read_handler, TSD_PERC_10_HANDLER);
    add_read_handler("perc25", read_handler, TSD_PERC_25_HANDLER);
    add_read_handler("median", read_handler, TSD_MED_HANDLER);
    add_read_handler("perc75", read_handler, TSD_PERC_75_HANDLER);
    add_read_handler("perc90", read_handler, TSD_PERC_90_HANDLER);
    add_read_handler("perc95", read_handler, TSD_PERC_95_HANDLER);
    add_read_handler("perc99", read_handler, TSD_PERC_99_HANDLER);
    add_read_handler("perc100", read_handler, TSD_PERC_100_HANDLER);
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

RecordTimestamp* TimestampDiff::get_recordtimestamp_instance() {
    return _rt;
}

void
TimestampDiff::min_mean_max(std::vector<unsigned> &vec, unsigned &min, double &mean, unsigned &max)
{
    double sum = 0.0;

    for (auto delay : vec) {
        sum += static_cast<double>(delay);
        if (delay < min) {
            min = delay;
        }
        if (delay > max) {
            max = delay;
        }
    }

    mean = sum / vec.size();
}

double
TimestampDiff::percentile(std::vector<unsigned> &vec, double percent)
{
    double perc = -1;

    // The size of the vector of latencies
    size_t vec_size = vec.size();

    // The desired percentile
    size_t idx = (percent * vec_size) / 100;

    // Implies empty vector, no percentile.
    if ((idx == 0) && (vec_size == 0)) {
        return perc;
    }
    // Implies that user asked for the 0 percetile (i.e., min).
    else if ((idx == 0) && (vec_size > 0)) {
        std::sort(vec.begin(), vec.end());
        perc = static_cast<double>(vec[0]);
        return perc;
    // Implies that user asked for the 100 percetile (i.e., max).
    } else if (idx == vec_size) {
        std::sort(vec.begin(), vec.end());
        perc = static_cast<double>(vec[vec_size - 1]);
        return perc;
    }

    auto nth = vec.begin() + idx;
    std::nth_element(vec.begin(), nth, vec.end());
    perc = static_cast<double>(*nth);

    return perc;
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel)
EXPORT_ELEMENT(TimestampDiff)
