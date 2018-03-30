/*
 * timestampdiff.{cc,hh} -- Store a packet count inside packet payload
 * Cyril Soldani, Tom Barbette
 *
 * Various latency percentiles by Georgios Katsikas
 *
 * Copyright (c) 2015-2016 University of Liège
 * Copyright (c) 2017 RISE SICS
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

TimestampDiff::TimestampDiff() :
    _delays(), _offset(40), _limit(0), _net_order(false), _max_delay_ms(1000)
{
    _nd = 0;
}

TimestampDiff::~TimestampDiff() {
}

int TimestampDiff::configure(Vector<String> &conf, ErrorHandler *errh)
{
    Element* e;
    if (Args(conf, this, errh)
            .read_mp("RECORDER", e)
            .read("OFFSET",_offset)
            .read("N", _limit)
            .read("MAXDELAY", _max_delay_ms)
            .complete() < 0)
        return -1;

    if (get_passing_threads().weight() > 1 && !_limit) {
        return errh->error("TimestampDiff is only thread safe if N is set");
    }

    if ((_rt = static_cast<RecordTimestamp*>(e->cast("RecordTimestamp"))) == 0)
        return errh->error("RECORDER must be a valid RecordTimestamp element");

    _net_order = _rt->has_net_order();

    if (_limit) {
        _delays.resize(_limit, 0);
    }

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
    TSD_LAST_SEEN,
    TSD_DUMP_HANDLER
};

String TimestampDiff::read_handler(Element *e, void *arg)
{
    TimestampDiff *tsd = static_cast<TimestampDiff *>(e);
    unsigned min = UINT_MAX;
    double  mean = 0.0;
    unsigned max = 0;

    // Return updated min, mean, and max values
    tsd->min_mean_max(min, mean, max);

    switch (reinterpret_cast<intptr_t>(arg)) {
        case TSD_MIN_HANDLER:
            return String(min);
        case TSD_AVG_HANDLER:
            return String(mean);
        case TSD_MAX_HANDLER:
            return String(max);
        case TSD_STD_HANDLER:
            return String(tsd->standard_deviation(mean));
        case TSD_PERC_00_HANDLER:
            return String(min);
        case TSD_PERC_01_HANDLER:
            return String(tsd->percentile(1));
        case TSD_PERC_05_HANDLER:
            return String(tsd->percentile(5));
        case TSD_PERC_10_HANDLER:
            return String(tsd->percentile(10));
        case TSD_PERC_25_HANDLER:
            return String(tsd->percentile(25));
        case TSD_MED_HANDLER:
            return String(tsd->percentile(50));
        case TSD_PERC_75_HANDLER:
            return String(tsd->percentile(75));
        case TSD_PERC_90_HANDLER:
            return String(tsd->percentile(90));
        case TSD_PERC_95_HANDLER:
            return String(tsd->percentile(95));
        case TSD_PERC_99_HANDLER:
            return String(tsd->percentile(99));
        case TSD_PERC_100_HANDLER:
            return String(max);
        case TSD_LAST_SEEN: {
            return String(tsd->last_value_seen());
        }
        case TSD_DUMP_HANDLER: {
            StringAccum s;
            for (size_t i = 0; i < tsd->_nd; ++i)
                s << i << ": " << String(tsd->_delays[i]) << "\n";
            return s.take_string();
        }
        default:
            return String("Unknown read handler for TimestampDiff");
    }
}

void TimestampDiff::add_handlers()
{
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
    add_read_handler("last", read_handler, TSD_LAST_SEEN);
    add_read_handler("dump", read_handler, TSD_DUMP_HANDLER);
}

inline int TimestampDiff::smaction(Packet *p)
{
    Timestamp now = Timestamp::now_steady();
    uint64_t i = NumberPacket::read_number_of_packet(p, _offset, _net_order);
    Timestamp old = get_recordtimestamp_instance()->get(i);
    if (old == Timestamp::uninitialized_t()) {
        return 1;
    }

    Timestamp diff = now - old;
    if (diff.msecval() > _max_delay_ms)
        click_chatter(
            "Packet %" PRIu64 " experienced delay %u ms > %u ms",
            i, (diff.sec() * 1000000 + diff.usec())/1000, _max_delay_ms
        );
    else {
        uint32_t next_index = _nd.fetch_and_add(1);
        if (_limit) {
            _delays[next_index] = diff.usec();
        } else {
            _delays.push_back(diff.usec());
        }
    }
    return 0;
}

void TimestampDiff::push(int, Packet *p)
{
    int o = smaction(p);
    checked_output_push(o, p);
}

#if HAVE_BATCH
void
TimestampDiff::push_batch(int, PacketBatch *batch)
{
    CLASSIFY_EACH_PACKET(2, smaction, batch, checked_output_push_batch);
}
#endif

RecordTimestamp* TimestampDiff::get_recordtimestamp_instance()
{
    return _rt;
}

void
TimestampDiff::min_mean_max(unsigned &min, double &mean, unsigned &max)
{
    const uint32_t current_vector_length = static_cast<const uint32_t>(_nd.value());
    double sum = 0.0;

    for (uint32_t i=0; i<current_vector_length; i++) {
        unsigned delay = _delays[i];

        sum += static_cast<double>(delay);
        if (delay < min) {
            min = delay;
        }
        if (delay > max) {
            max = delay;
        }
    }

    // Set minimum properly if not updated above
    if (min == UINT_MAX) {
        min = 0;
    }

    if (current_vector_length == 0) {
        mean = 0.0;
        return;
    }
    mean = sum / static_cast<double>(current_vector_length);
}

double
TimestampDiff::standard_deviation(const double mean)
{
    const uint32_t current_vector_length = static_cast<const uint32_t>(_nd.value());
    double var = 0.0;

    for (uint32_t i=0; i<current_vector_length; i++) {
        var += pow(_delays[i] - mean, 2);
    }

    // Prevent square root of zero
    if (var == 0) {
        return static_cast<double>(0);
    }

    return sqrt(var / current_vector_length);
}

double
TimestampDiff::percentile(const double percent)
{
    double perc = 0;

    const uint32_t current_vector_length = static_cast<const uint32_t>(_nd.value());

    // The desired percentile
    size_t idx = (percent * current_vector_length) / 100;

    // Implies empty vector, no percentile.
    if ((idx == 0) && (current_vector_length == 0)) {
        return perc;
    }
    // Implies that user asked for the 0 percetile (i.e., min).
    else if ((idx == 0) && (current_vector_length > 0)) {
        std::sort(_delays.begin(), _delays.end());
        perc = static_cast<double>(_delays[0]);
        return perc;
    // Implies that user asked for the 100 percetile (i.e., max).
    } else if (idx == current_vector_length) {
        std::sort(_delays.begin(), _delays.end());
        perc = static_cast<double>(_delays[current_vector_length - 1]);
        return perc;
    }

    auto nth = _delays.begin() + idx;
    std::nth_element(_delays.begin(), nth, _delays.begin() + current_vector_length);
    perc = static_cast<double>(*nth);

    return perc;
}

unsigned
TimestampDiff::last_value_seen()
{
    const int32_t last_vector_index = static_cast<const int32_t>(_nd.value() - 1);

    if (last_vector_index < 0) {
        return 0;
    }

    return _delays[last_vector_index];
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel)
EXPORT_ELEMENT(TimestampDiff)
