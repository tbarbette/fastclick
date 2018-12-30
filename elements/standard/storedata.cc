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
#include "storedata.hh"
#include <click/args.hh>
#include <click/error.hh>
CLICK_DECLS

StoreData::StoreData() : _grow(false)
{
}

int
StoreData::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
        .read_mp("OFFSET", _offset)
        .read_mp("DATA", _data)
        .read_p("MASK", _mask)
        .read("GROW", _grow)
        .complete() < 0)
        return -1;

    if (_mask && _mask.length() > _data.length())
        return errh->error("MASK must be no longer than DATA");

    return 0;
}

int
StoreData::initialize(ErrorHandler *)
{
    if (_mask) {
        auto md = _data.mutable_data();
        for (int i = 0; i < _mask.length(); i++)
            md[i] = md[i] & _mask[i];
    }
    return 0;
}

Packet *
StoreData::simple_action(Packet *p)
{
    if (p->length() <= _offset)
        return p;
    else if (WritablePacket *q = p->uniqueify()) {
        int len = q->length() - _offset;
        if (_grow && _data.length() > len) {
            q = q->put(_data.length() - len);
            len = q->length() - _offset;
        }
        if (_mask) {
            auto qd = q->data();
            for (int i = 0; i < (_data.length() < len ? _data.length() : len); i++) {
                if (i < _mask.length())
                    qd[_offset + i] = (qd[_offset + i] & ~_mask[i]) | _data[i];
                else
                    qd[_offset + i] = _data[i];
            }
        }
        else
            memcpy(q->data() + _offset, _data.data(), (_data.length() < len ? _data.length() : len));
        return q;
    } else
        return 0;
}

#if HAVE_BATCH
PacketBatch *
StoreData::simple_action_batch(PacketBatch *head)
{
    EXECUTE_FOR_EACH_PACKET_DROPPABLE(StoreData::simple_action,head,[](Packet*){});
    return head;
}
#endif

CLICK_ENDDECLS
EXPORT_ELEMENT(StoreData)
ELEMENT_MT_SAFE(StoreData)
