// -*- c-basic-offset: 4 -*-
/*
 * averagebatchcounter.{cc,hh} -- provides batch-related statistics
 * Tom Barbette, Georgios Katsikas
 *
 * Copyright (c) 2017 University of Liege
 * Copyright (c) 2019 KTH Royal Institute of Technology
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
#include "averagebatchcounter.hh"
#include <click/string.hh>
#include <click/straccum.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/routervisitor.hh>
#include <click/router.hh>

CLICK_DECLS

AverageBatchCounter::AverageBatchCounter() : _interval(1000), _frame_len_stats(false), _timer(this)
{
    in_batch_mode = BATCH_MODE_NEEDED;
}

AverageBatchCounter::~AverageBatchCounter()
{

}

int
AverageBatchCounter::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
        .read_p("INTERVAL", _interval)
        .read("LENGTH_STATS", _frame_len_stats)
        .complete() < 0)
        return -1;

    return 0;
}

int
AverageBatchCounter::initialize(ErrorHandler *errh)
{
    // Uses an upstream AggregateLength to get frame length
    if (_frame_len_stats) {
        ElementCastTracker filter(router(), "AggregateLength");
        router()->visit_upstream(this, 0, &filter);
        if (filter.elements().size() == 0) {
            errh->error("Could not find upstream AggregateLength element: LENGTH_STATS not available");
            _frame_len_stats = false;
        }
    }

    _timer.initialize(this);
    _timer.schedule_after_msec(_interval);

    return 0;
}

void
AverageBatchCounter::cleanup(CleanupStage)
{

}

void
AverageBatchCounter::run_timer(Timer *t)
{
    BatchStats &total = _stats_total.write_begin();
    BatchStats &last_tick = _stats_last_tick.write_begin();
    total.count_batches += last_tick.count_batches;
    total.count_packets += last_tick.count_packets;
    if (_frame_len_stats) {
        if (last_tick.count_packets > 0) {
            total.count_bytes += last_tick.count_bytes;
            total.avg_frame_len = (float) total.count_bytes / (float) total.count_packets;
        }
    }
    last_tick.count_batches = 0;
    last_tick.count_packets = 0;
    last_tick.count_bytes = 0;
    last_tick.avg_frame_len = 0;
    for (unsigned i = 0; i < _stats.weight(); i++) {
        last_tick.count_batches += _stats.get_value(i).count_batches;
        last_tick.count_packets += _stats.get_value(i).count_packets;
        if (_frame_len_stats) {
            if (_stats.get_value(i).count_packets > 0) {
                last_tick.count_bytes += _stats.get_value(i).count_bytes;
                last_tick.avg_frame_len = (float) last_tick.count_bytes / (float) last_tick.count_packets;
            }
        }
        _stats.get_value(i).count_batches = 0;
        _stats.get_value(i).count_packets = 0;
        _stats.get_value(i).count_bytes = 0;
        _stats.get_value(i).avg_frame_len = 0;
    }
    _stats_total.write_commit();
    _stats_last_tick.write_commit();

    t->reschedule_after_msec(_interval);
}

PacketBatch *
AverageBatchCounter::simple_action_batch(PacketBatch *b)
{
    BatchStats &stat = *_stats;
    stat.count_batches ++;
    stat.count_packets += b->count();
    if (_frame_len_stats) {
        if (b->count() > 0) {
            stat.count_bytes += (uint64_t) compute_agg_frame_len(b);
            stat.avg_frame_len = (float) stat.count_bytes / (float) stat.count_packets;
        }
    }

    return b;
}

uint32_t
AverageBatchCounter::compute_agg_frame_len(PacketBatch *batch)
{
    uint32_t agg_len = 0;
    FOR_EACH_PACKET(batch, p) {
        uint32_t value = AGGREGATE_ANNO(p);
        if (value > 0) {
            agg_len += value;
        } else {
            // The upstream AggregateLength must prevent this from happening
            assert(false);
        }
    }

    return agg_len;
}

String
AverageBatchCounter::read_handler(Element *e, void *thunk)
{
    AverageBatchCounter *fd = static_cast<AverageBatchCounter *>(e);
    switch ((intptr_t)thunk) {
        case H_AVERAGE: {
            BatchStats stat = fd->_stats_last_tick.read();
            if (stat.count_batches == 0)
                return "0";
            return String((float) stat.count_packets / (float) stat.count_batches);
        }
        case H_AVERAGE_TOTAL: {
            BatchStats stat = fd->_stats_total.read();
            if (stat.count_batches == 0)
                return "0";
            return String((float) stat.count_packets / (float) stat.count_batches);
        }
        case H_COUNT_BATCHES: {
            BatchStats stat = fd->_stats_last_tick.read();
            return String(stat.count_batches);
        }
        case H_COUNT_BATCHES_TOTAL: {
            BatchStats stat = fd->_stats_total.read();
            return String(stat.count_batches);
        }
        case H_COUNT_PACKETS: {
            BatchStats stat = fd->_stats_last_tick.read();
            return String(stat.count_packets);
        }
        case H_COUNT_PACKETS_TOTAL: {
            BatchStats stat = fd->_stats_total.read();
            return String(stat.count_packets);
        }
        case H_AVG_FRAME_LEN: {
            BatchStats stat = fd->_stats_last_tick.read();
            return String(stat.avg_frame_len);
        }
        case H_AVG_FRAME_LEN_TOTAL: {
            BatchStats stat = fd->_stats_total.read();
            return String(stat.avg_frame_len);
        }
        default: {
            return "-1";
        }
    }
}

void
AverageBatchCounter::add_handlers()
{
    add_read_handler("average", read_handler, H_AVERAGE);
    add_read_handler("average_total", read_handler, H_AVERAGE_TOTAL);
    add_read_handler("count_packets", read_handler, H_COUNT_PACKETS);
    add_read_handler("count_packets_total", read_handler, H_COUNT_PACKETS_TOTAL);
    add_read_handler("count_batches", read_handler, H_COUNT_BATCHES);
    add_read_handler("count_batches_total", read_handler, H_COUNT_BATCHES_TOTAL);
    add_read_handler("average_frame_len", read_handler, H_AVG_FRAME_LEN);
    add_read_handler("average_frame_len_total", read_handler, H_AVG_FRAME_LEN_TOTAL);
}


CLICK_ENDDECLS
ELEMENT_REQUIRES(batch)
EXPORT_ELEMENT(AverageBatchCounter)
ELEMENT_MT_SAFE(AverageBatchCounter)
