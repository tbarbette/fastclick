// -*- c-basic-offset: 4 -*-
/*
 * storedata.{cc,hh} -- element changes packet data
 * Eddie Kohler
 *
 * Copyright (c) 2004 Regents of the University of California
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
#include "storeanno.hh"
#include <click/args.hh>
#include <click/error.hh>
CLICK_DECLS

StoreAnno::StoreAnno() : _grow(false)
{
}

int
StoreAnno::configure(Vector<String> &conf, ErrorHandler *errh)
{
    int anno = PAINT_ANNO_OFFSET;

    if (Args(conf, this, errh)
        .read_mp("OFFSET", _offset)
	    .read_p("ANNO", AnnoArg(1), anno)
        .read("GROW", _grow)
        .complete() < 0)
        return -1;

    _anno = anno;

    return 0;
}

int
StoreAnno::initialize(ErrorHandler *)
{
    return 0;
}

Packet *
StoreAnno::simple_action(Packet *p)
{
    if (p->length() <= _offset)
        return p;
    else if (WritablePacket *q = p->uniqueify()) {
        int len = q->length() - _offset;
        if (_grow && 1 > len) {
            q = q->put(1 - len);
            len = q->length() - _offset;
        }

        *(q->data() + _offset) = p->anno_u8(_anno);
        return q;
    } else
        return 0;
}

#if HAVE_BATCH
PacketBatch *
StoreAnno::simple_action_batch(PacketBatch *head)
{
    EXECUTE_FOR_EACH_PACKET_DROPPABLE(StoreAnno::simple_action,head,[](Packet*){});
    return head;
}
#endif

CLICK_ENDDECLS
EXPORT_ELEMENT(StoreAnno)
ELEMENT_MT_SAFE(StoreAnno)
