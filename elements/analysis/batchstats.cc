// -*- c-basic-offset: 4 -*-
/*
 * batchstats.{cc,hh} -- batch statistics counter
 * Tom Barbette
 *
 * Copyright (c) 2016 University of Liege
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
#include "batchstats.hh"
#include <click/string.hh>
#include <click/straccum.hh>
#include <click/args.hh>

CLICK_DECLS

BatchStats::BatchStats() : StatVector(Vector<int>(MAX_BATCH_SIZE,0))
{
}

BatchStats::~BatchStats()
{
}
int
BatchStats::configure(Vector<String> &conf, ErrorHandler *errh)
{

    if (Args(conf, this, errh)
	.complete() < 0)
	return -1;

    return 0;
}

void *
BatchStats::cast(const char *name)
{
    if (strcmp(name, "StatVector") == 0)
	return (StatVector*)this;
    else
	return Element::cast(name);
}


int
BatchStats::initialize(ErrorHandler *)
{
    return 0;
}

void
BatchStats::cleanup(CleanupStage)
{

}

Packet*
BatchStats::simple_action(Packet* p)
{
    (*stats)[1]++;
    return p;
}

#if HAVE_BATCH
PacketBatch*
BatchStats::simple_action_batch(PacketBatch* b)
{
    (*stats)[b->count()]++;
    return b;
}
#endif



void
BatchStats::add_handlers()
{
    add_stat_handler(this);
}


CLICK_ENDDECLS

ELEMENT_REQUIRES(batch)
EXPORT_ELEMENT(BatchStats)
ELEMENT_MT_SAFE(BatchStats)
