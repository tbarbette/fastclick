/*
 * StripTransportHeader.{cc,hh} -- element removes IP header based on annotation
 * Eddie Kohler
 *
 * Computational batching support
 * by Georgios Katsikas
 *
 * Copyright (c) 2000 Massachusetts Institute of Technology
 * Copyright (c) 2016 KTH Royal Institute of Technology
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
#include "striptransportheader.hh"
#include <clicknet/ip.h>
CLICK_DECLS

StripTransportHeader::StripTransportHeader()
{
}

StripTransportHeader::~StripTransportHeader()
{
}

Packet *
StripTransportHeader::simple_action(Packet *p)
{
    unsigned l = p->transport_header_offset();
    l += transport_header_length(this, p);
    p->pull(l);
    return p;
}

#if HAVE_BATCH
PacketBatch*
StripTransportHeader::simple_action_batch(PacketBatch *batch)
{
    EXECUTE_FOR_EACH_PACKET(StripTransportHeader::simple_action, batch);
    return batch;
}
#endif

CLICK_ENDDECLS
EXPORT_ELEMENT(StripTransportHeader)
EXPORT_ELEMENT(StripTransportHeader-StripTCPHeader)
EXPORT_ELEMENT(StripTransportHeader-StripUDPHeader)
ELEMENT_MT_SAFE(StripTransportHeader)
