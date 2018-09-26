/*
 * timestampdiff.{cc,hh} -- Compute the difference between the recorded
 * timestamp of a packet using RecordTimestamp and a fresh timestamp
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
    _delays(), _offset(40), _limit(0), _net_order(false), _max_delay_ms(1000), _verbose(true)
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
            .read("VERBOSE", _verbose)
            .complete() < 0)
        return -1;

    if ((_rt = static_cast<RecordTimestamp*>(e->cast("RecordTimestamp"))) == 0)
        return errh->error("RECORDER must be a valid RecordTimestamp element");

    _net_order = _rt->has_net_order();

    if (_limit) {
        _delays.resize(_limit, 0);
    }

    return 0;
}

int TimestampDiff::initialize(ErrorHandler *errh)
{
    if (get_passing_threads().weight() > 1 && !_limit) {
        return errh->error("TimestampDiff is only thread safe if N is set");
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
    TSD_PERC_HANDLER,
    TSD_LAST_SEEN,
    TSD_CURRENT_INDEX,
    TSD_DUMP_HANDLER
};

int TimestampDiff::handler(int operation, String &data, Element *e,
        const Handler *handler, ErrorHandler *errh)
{
    TimestampDiff *tsd = static_cast<TimestampDiff *>(e);
    unsigned min = UINT_MAX;
    double  mean = 0.0;
    unsigned max = 0;
    unsigned begin = 0;
    double perc = 0;
    int opt = reinterpret_cast<intptr_t>(handler->user_data(Handler::f_read));

    if (data != "") {
        if (opt == TSD_PERC_HANDLER) {
            int pos = data.find_left(' ');
            if (pos == -1) pos = data.length();
            if (!DoubleArg().parse(data.substring(0, pos), perc)) {
                data = "<error>";
                return -1;
            }
            data = data.substring(pos);
        }
        begin = atoi(data.c_str());
        const uint32_t current_vector_length = static_cast<const uint32_t>(tsd->_nd.value());

        if (begin >= current_vector_length) {
               data = 0;
               return 1;
        }
    }

    switch (opt) {
        case TSD_MIN_HANDLER:
            tsd->min_mean_max(min, mean, max, begin);
            data = String(min); break;
        case TSD_AVG_HANDLER:
            tsd->min_mean_max(min, mean, max, begin);
            data = String(mean); break;
        case TSD_MAX_HANDLER:
            tsd->min_mean_max(min, mean, max, begin);
            data = String(max); break;
        case TSD_STD_HANDLER:
            data = String(tsd->standard_deviation(mean)); break;
        case TSD_PERC_00_HANDLER:
            tsd->min_mean_max(min, mean, max, begin);
            data = String(min); break;
        case TSD_PERC_01_HANDLER:
            data = String(tsd->percentile(1)); break;
        case TSD_PERC_05_HANDLER:
            data = String(tsd->percentile(5)); break;
        case TSD_PERC_10_HANDLER:
            data = String(tsd->percentile(10)); break;
        case TSD_PERC_25_HANDLER:
            data = String(tsd->percentile(25)); break;
        case TSD_MED_HANDLER:
            data = String(tsd->percentile(50)); break;
        case TSD_PERC_75_HANDLER:
            data = String(tsd->percentile(75)); break;
        case TSD_PERC_90_HANDLER:
            data = String(tsd->percentile(90)); break;
        case TSD_PERC_95_HANDLER:
            data = String(tsd->percentile(95)); break;
        case TSD_PERC_99_HANDLER:
            data = String(tsd->percentile(99)); break;
        case TSD_PERC_100_HANDLER:
            tsd->min_mean_max(min, mean, max, begin);
            data = String(max); break;
        case TSD_PERC_HANDLER:
            data = String(tsd->percentile(perc)); break;
        case TSD_LAST_SEEN:
            data = String(tsd->last_value_seen()); break;
        case TSD_CURRENT_INDEX: {
            const int32_t last_vector_index = static_cast<const int32_t>(tsd->_nd.value() - 1);
            data = String(last_vector_index); break;
        }
        case TSD_DUMP_HANDLER: {
            StringAccum s;
            for (size_t i = 0; i < tsd->_nd; ++i)
                s << i << ": " << String(tsd->_delays[i]) << "\n";
            data = s.take_string(); break;
        }
        default:
            data = String("Unknown read handler for TimestampDiff"); break;
    }
    return 0;
}

void TimestampDiff::add_handlers()
{
    set_handler("average", Handler::f_read | Handler::f_read_param, handler, TSD_AVG_HANDLER, 0);
    set_handler("avg", Handler::f_read | Handler::f_read_param, handler, TSD_AVG_HANDLER, 0);

    set_handler("min", Handler::f_read | Handler::f_read_param, handler, TSD_MIN_HANDLER, 0);
    set_handler("max", Handler::f_read | Handler::f_read_param, handler, TSD_MAX_HANDLER, 0);
    set_handler("stddev", Handler::f_read | Handler::f_read_param, handler, TSD_STD_HANDLER, 0);
    set_handler("perc00", Handler::f_read | Handler::f_read_param, handler, TSD_PERC_00_HANDLER, 0);
    set_handler("perc01", Handler::f_read | Handler::f_read_param, handler, TSD_PERC_01_HANDLER, 0);
    set_handler("perc05", Handler::f_read | Handler::f_read_param, handler, TSD_PERC_05_HANDLER, 0);
    set_handler("perc10", Handler::f_read | Handler::f_read_param, handler, TSD_PERC_10_HANDLER, 0);
    set_handler("perc25", Handler::f_read | Handler::f_read_param, handler, TSD_PERC_25_HANDLER, 0);
    set_handler("median", Handler::f_read | Handler::f_read_param, handler, TSD_MED_HANDLER, 0);
    set_handler("perc75", Handler::f_read | Handler::f_read_param, handler, TSD_PERC_75_HANDLER, 0);
    set_handler("perc90", Handler::f_read | Handler::f_read_param, handler, TSD_PERC_90_HANDLER, 0);
    set_handler("perc95", Handler::f_read | Handler::f_read_param, handler, TSD_PERC_95_HANDLER, 0);
    set_handler("perc99", Handler::f_read | Handler::f_read_param, handler, TSD_PERC_99_HANDLER, 0);
    set_handler("perc100", Handler::f_read | Handler::f_read_param, handler, TSD_PERC_100_HANDLER, 0);
    set_handler("perc", Handler::f_read | Handler::f_read_param, handler, TSD_PERC_HANDLER, 0);
    set_handler("index", Handler::f_read, handler, TSD_CURRENT_INDEX, 0);
    set_handler("last", Handler::f_read, handler, TSD_LAST_SEEN, 0);
    set_handler("dump", Handler::f_read, handler, TSD_DUMP_HANDLER, 0);
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
    if ((diff.msecval() > _max_delay_ms)) {
        if (_verbose) {
            click_chatter(
                "Packet %" PRIu64 " experienced delay %u ms > %u ms",
                i, (diff.sec() * 1000000 + diff.usec())/1000, _max_delay_ms
            );
        }
    }
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
TimestampDiff::min_mean_max(unsigned &min, double &mean, unsigned &max, uint32_t begin)
{
    const uint32_t current_vector_length = static_cast<const uint32_t>(_nd.value());
    double sum = 0.0;

    for (uint32_t i=begin; i<current_vector_length; i++) {
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
    mean = sum / static_cast<double>(current_vector_length - begin);
}

double
TimestampDiff::standard_deviation(const double mean, uint32_t begin)
{
    const uint32_t current_vector_length = static_cast<const uint32_t>(_nd.value());
    double var = 0.0;

    for (uint32_t i=begin; i<current_vector_length; i++) {
        var += pow(_delays[i] - mean, 2);
    }

    // Prevent square root of zero
    if (var == 0) {
        return static_cast<double>(0);
    }

    return sqrt(var / current_vector_length);
}

double
TimestampDiff::percentile(const double percent, uint32_t begin)
{
    double perc = 0;

    const uint32_t current_vector_length = static_cast<const uint32_t>(_nd.value());

    // The desired percentile
    size_t idx = (percent * current_vector_length) / 100;

    // Implies empty vector, no percentile.
    if ((idx == begin) && (current_vector_length == begin)) {
        return perc;
    }
    // Implies that user asked for the 0 percetile (i.e., min).
    else if ((idx == begin) && (current_vector_length > begin)) {
        std::sort(_delays.begin(), _delays.end());
        perc = static_cast<double>(_delays[begin]);
        return perc;
    // Implies that user asked for the 100 percetile (i.e., max).
    } else if (idx == current_vector_length) {
        std::sort(_delays.begin() + begin, _delays.end());
        perc = static_cast<double>(_delays[current_vector_length - 1]);
        return perc;
    }

    auto nth = _delays.begin() + begin + idx;
    std::nth_element(_delays.begin() + begin, nth, _delays.begin() + current_vector_length);
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
