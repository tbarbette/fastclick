// -*- c-basic-offset: 4 -*-
/*
 * ratedunqueue.{cc,hh} -- element pulls as many packets as possible from
 * its input, pushes them out its output
 * Eddie Kohler
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2010 Meraki, Inc.
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
#include <click/args.hh>
#include "bwratedunqueue.hh"
CLICK_DECLS

BandwidthRatedUnqueue::BandwidthRatedUnqueue() : _use_extra_length(false), _link_rate(false)
{
}

int
BandwidthRatedUnqueue::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(this, errh).bind(conf)
        .read_or_set("EXTRA_LENGTH", _use_extra_length, false)
        .read_or_set("LINK_RATE", _link_rate, false)
        .consume() < 0)
        return -1;
    return RatedUnqueue::configure(conf, errh);
}

#define LENGTHOF(p) \
    ((_link_rate?8:1)*(p->length() + (_use_extra_length? EXTRA_LENGTH_ANNO(p) : 0)) + (_link_rate? 8 * 24:0))

bool
BandwidthRatedUnqueue::run_task(Task *)
{
    bool worked = false;
    _runs++;

    if (!_active)
	    return false;

    _tb.refill();

    if (_tb.contains(tb_bandwidth_thresh)) {
#if HAVE_BATCH
        if (in_batch_mode) {
            PacketBatch* batch = input(0).pull_batch(_burst);
            if (batch) {
                int c = 0;
                FOR_EACH_PACKET(batch, p) c += LENGTHOF(p);
                _tb.remove(c);
                _packets += batch->count();
                _pushes++;
                worked = true;
                output(0).push_batch(batch);
            } else {
                _failed_pulls++;
                if (!_signal)
                    return false; // without rescheduling
            }
        } else
#endif
        {
            if (Packet *p = input(0).pull()) {
                _tb.remove(LENGTHOF(p));
                _packets++;
                _pushes++;
                worked = true;
                output(0).push(p);
            } else {
                _failed_pulls++;
                if (!_signal)
                    return false; // without rescheduling
            }
        }
    } else {
	    _timer.schedule_after(Timestamp::make_jiffies(_tb.time_until_contains(tb_bandwidth_thresh)));
	    _empty_runs++;
	    return false;
    }
    _task.fast_reschedule();
    return worked;
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(RatedUnqueue)
EXPORT_ELEMENT(BandwidthRatedUnqueue)
ELEMENT_MT_SAFE(BandwidthRatedUnqueue)
