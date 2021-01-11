/*
 * ethermirror.{cc,hh} -- rewrites Ethernet packet a->b to b->a
 * Eddie Kohler
 *
 * Computational batching support
 * by Georgios Katsikas
 *
 * Copyright (c) 2000 Massachusetts Institute of Technology
 * Copyright (c) 2017 KTH Royal Institute of Technology
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
#include "ethermirror.hh"
#include <clicknet/ether.h>
CLICK_DECLS

EtherMirror::EtherMirror()
{
}

EtherMirror::~EtherMirror()
{
}

Packet *
EtherMirror::simple_action(Packet *p)
{
    if (WritablePacket *q = p->uniqueify()) {
        click_ether *ethh = reinterpret_cast<click_ether *>(q->data());
        uint8_t tmpa[6];
        memcpy(tmpa, ethh->ether_dhost, 6);
        memcpy(ethh->ether_dhost, ethh->ether_shost, 6);
        memcpy(ethh->ether_shost, tmpa, 6);

        return q;
    }

    return 0;
}

#if HAVE_BATCH
PacketBatch *
EtherMirror::simple_action_batch(PacketBatch *batch)
{
#ifdef CLICK_NOINDIRECT
    FOR_EACH_PACKET(batch, p)   {
        EtherMirror::simple_action(p);
    }
#else
    EXECUTE_FOR_EACH_PACKET_DROPPABLE(EtherMirror::simple_action, batch, [](Packet*){});
#endif
    return batch;
}
#endif

CLICK_ENDDECLS
EXPORT_ELEMENT(EtherMirror)
