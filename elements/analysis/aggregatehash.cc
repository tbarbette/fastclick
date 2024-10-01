/*
 * aggregatehash.{cc,hh} -- set aggregate annotation based on hash annotation
 * Eddie Kohler
 *
 * Copyright (c) 2006 Regents of the University of California
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
#include "aggregatehash.hh"
#include <click/error.hh>
#include <click/args.hh>
#include <click/packet_anno.hh>
#include <click/ipflowid.hh>

CLICK_DECLS

AggregateHash::AggregateHash()
{
}

AggregateHash::~AggregateHash()
{
}

int
AggregateHash::configure(Vector<String> &conf, ErrorHandler *errh)
{
    _bits = 8; //XXX TODO
    if (Args(conf, this, errh)
	.read_p("BITS", _bits)
	.complete() < 0)
	return -1;
    if (_bits <= 0 || _bits > 8)
	return errh->error("bad number of bits");
    return 0;
}

Packet *
AggregateHash::simple_action(Packet *p)
{
    uint32_t agg = IPFlowID(p).hashcode();

	SET_AGGREGATE_ANNO(p, agg);

    return p;
}

ELEMENT_REQUIRES(userlevel)
EXPORT_ELEMENT(AggregateHash)
CLICK_ENDDECLS
