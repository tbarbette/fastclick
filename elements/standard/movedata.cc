// -*- c-basic-offset: 4 -*-
/*
 * storedata.{cc,hh} -- element changes packet data
 * Tom Barbette
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
#include "movedata.hh"
#include <click/args.hh>
#include <click/error.hh>
CLICK_DECLS

MoveData::MoveData() : _grow(true)
{
}

int
MoveData::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
        .read_mp("DST_OFFSET", _dst_offset)
        .read_mp("SRC_OFFSET", _src_offset)
        .read_mp("LENGTH", _length)
        .read("GROW", _grow)
        .complete() < 0)
        return -1;

    return 0;
}

int
MoveData::initialize(ErrorHandler *)
{

    return 0;
}

Packet *
MoveData::simple_action(Packet *p)
{
    if (p->length() <= _src_offset + _length) {
        return p;
    }
    else if (WritablePacket *q = p->uniqueify()) {
        if (_dst_offset + (int)_length > (int)q->length()) {
            if (_grow)
                q = q->put(_dst_offset + _length - q->length());
            else
                return q;
        }
        memcpy(q->data() + _dst_offset, q->data() + _src_offset, _length);
        return q;
    } else
        return 0;
}

#if HAVE_BATCH
PacketBatch *
MoveData::simple_action_batch(PacketBatch *head)
{
    EXECUTE_FOR_EACH_PACKET_DROPPABLE(MoveData::simple_action,head,[](Packet*){});
    return head;
}
#endif

CLICK_ENDDECLS
EXPORT_ELEMENT(MoveData)
ELEMENT_MT_SAFE(MoveData)
