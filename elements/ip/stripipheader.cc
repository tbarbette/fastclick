/*
 * stripipheader.{cc,hh} -- element removes IP header based on annotation
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
#include "stripipheader.hh"
#include <clicknet/ip.h>
CLICK_DECLS

StripIPHeader::StripIPHeader()
{
}

StripIPHeader::~StripIPHeader()
{
}

Packet *
StripIPHeader::simple_action(Packet *p)
{
    p->pull(p->transport_header_offset());
    return p;
}

#if HAVE_BATCH
PacketBatch*
StripIPHeader::simple_action_batch(PacketBatch *batch)
{
    EXECUTE_FOR_EACH_PACKET(StripIPHeader::simple_action, batch);
    return batch;
}
#endif

CLICK_ENDDECLS
EXPORT_ELEMENT(StripIPHeader)
ELEMENT_MT_SAFE(StripIPHeader)
