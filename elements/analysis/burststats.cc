// -*- c-basic-offset: 4 -*-
/*
 * burststats.{cc,hh} -- burst statistics counter
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
#include "burststats.hh"
#include <click/string.hh>
#include <click/straccum.hh>
#include <click/args.hh>

CLICK_DECLS

BurstStats::BurstStats() : StatVector(Vector<int>(1024,0))
{
}

BurstStats::~BurstStats()
{
}
int
BurstStats::configure(Vector<String> &conf, ErrorHandler *errh)
{

    if (Args(conf, this, errh)
	.complete() < 0)
	return -1;

    return 0;
}

void *
BurstStats::cast(const char *name)
{
    if (strcmp(name, "StatVector") == 0)
	return (StatVector*)this;
    else
	return Element::cast(name);
}


int
BurstStats::initialize(ErrorHandler *errh)
{
    return 0;
}

void
BurstStats::cleanup(CleanupStage)
{

}

Packet*
BurstStats::simple_action(Packet* p)
{
    if (AGGREGATE_ANNO(p) != s->last_anno) {
        if (s->burstlen >= 1024)
            s->burstlen = 1023;
        (*stats)[s->burstlen]++;
        s->burstlen = 1;
        s->last_anno = AGGREGATE_ANNO(p);
    } else {
       s->burstlen++;
    }

    return p;
}

void
BurstStats::add_handlers()
{
    add_stat_handler(this);
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(batch)
EXPORT_ELEMENT(BurstStats)
ELEMENT_MT_SAFE(BurstStats)
