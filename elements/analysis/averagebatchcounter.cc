// -*- c-basic-offset: 4 -*-
/*
 * averagebatchcounter.{cc,hh} -- anonymize packet IP addresses
 * Tom Barbette
 *
 * Copyright (c) 2017 University of Liege
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

CLICK_DECLS

AverageBatchCounter::AverageBatchCounter() : _interval(1000), _timer(this)
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
	.complete() < 0)
	return -1;

    return 0;
}

int
AverageBatchCounter::initialize(ErrorHandler *errh)
{
    (void)errh;
    _timer.initialize(this);
    _timer.schedule_after_msec(_interval);
    return 0;
}

void
AverageBatchCounter::cleanup(CleanupStage)
{

}

void
AverageBatchCounter::run_timer(Timer* t)
{
    BatchStats &total = _stats_total.write_begin();
    BatchStats &last_tick = _stats_last_tick.write_begin();
    total.count_batches += last_tick.count_batches;
    total.count_packets += last_tick.count_packets;
    last_tick.count_batches = 0;
    last_tick.count_packets = 0;
    for (unsigned i = 0; i < _stats.weight(); i++) {
        last_tick.count_batches += _stats.get_value(i).count_batches;
        last_tick.count_packets += _stats.get_value(i).count_packets;
        _stats.get_value(i).count_batches = 0;
        _stats.get_value(i).count_packets = 0;
    }
    _stats_total.write_commit();
    _stats_last_tick.write_commit();

    t->reschedule_after_msec(_interval);
}

PacketBatch*
AverageBatchCounter::simple_action_batch(PacketBatch* b)
{
    BatchStats &stat = *_stats;
    stat.count_batches ++;
    stat.count_packets += b->count();
    return b;
}

String
AverageBatchCounter::read_handler(Element *e, void *thunk)
{
    AverageBatchCounter *fd = static_cast<AverageBatchCounter *>(e);
    switch ((intptr_t)thunk) {
      case H_AVERAGE: {
          BatchStats stat = fd->_stats_last_tick.read();
          if (stat.count_batches == 0)
              return 0;
          return String(stat.count_packets / stat.count_batches);
      }
      case H_AVERAGE_TOTAL: {
          BatchStats stat = fd->_stats_total.read();
          if (stat.count_batches == 0)
                return 0;
          return String(stat.count_packets / stat.count_batches);
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
      default:
      return "-1";
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
}


CLICK_ENDDECLS
ELEMENT_REQUIRES(batch)
EXPORT_ELEMENT(AverageBatchCounter)
ELEMENT_MT_SAFE(AverageBatchCounter)
