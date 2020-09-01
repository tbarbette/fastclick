// -*- c-basic-offset: 4 -*-
/*
 * aggregatestats.{cc,hh} -- aggregate statistics counter
 * Tom Barbette
 *
 * Copyright (c) 2020 KTH Royal Institute of Technology
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
#include "aggstats.hh"
#include <click/string.hh>
#include <click/straccum.hh>
#include <click/args.hh>

CLICK_DECLS

AggregateStats::AggregateStats() : StatVector(Vector<int>())
{
}

AggregateStats::~AggregateStats()
{
}
int
AggregateStats::configure(Vector<String> &conf, ErrorHandler *errh)
{

    if (Args(conf, this, errh)
            .read_mp("MAX", _max)
	.complete() < 0)
	return -1;

    return 0;
}

void *
AggregateStats::cast(const char *name)
{
    if (strcmp(name, "StatVector") == 0)
	return (StatVector*)this;
    else
	return Element::cast(name);
}


int
AggregateStats::initialize(ErrorHandler *errh)
{
    resize_stats(_max, 0);
    return 0;
}

void
AggregateStats::cleanup(CleanupStage)
{

}

Packet*
AggregateStats::simple_action(Packet* p)
{
    (*stats)[AGGREGATE_ANNO(p)]++;
    return p;
}

#if HAVE_BATCH
PacketBatch*
AggregateStats::simple_action_batch(PacketBatch* b)
{
    FOR_EACH_PACKET(b,p)
        (*stats)[AGGREGATE_ANNO(p)]++;
    return b;
}
#endif



void
AggregateStats::add_handlers()
{
    add_stat_handler(this);
}


CLICK_ENDDECLS

ELEMENT_REQUIRES(batch)
EXPORT_ELEMENT(AggregateStats)
ELEMENT_MT_SAFE(AggregateStats)
