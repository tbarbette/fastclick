/*
 * hashswitch.{cc,hh} -- element demultiplexes packets based on hash of
 * specified packet fields
 * Eddie Kohler
 *
 * Computational batching support by Georgios Katsikas
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2018 Georgios Katsikas, RISE SICS
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
#include "hashswitch.hh"
#include <click/error.hh>
#include <click/args.hh>
CLICK_DECLS

HashSwitch::HashSwitch() : _offset(-1)
{
}

int
HashSwitch::configure(Vector<String> &conf, ErrorHandler *errh)
{
    _max = noutputs();
    if (Args(conf, this, errh)
        .read_mp("OFFSET", _offset)
        .read_mp("LENGTH", _length)
        .read("MAX", _max)
        .complete() < 0)
    return -1;

    if (_length == 0)
        return errh->error("length must be > 0");

    return 0;
}

int
HashSwitch::process(Packet *p)
{
    const unsigned char *data = p->data();
    int o = _offset, l = _length;
    if ((int)p->length() < o + l)
        return 0;
    else {
        int d = 0;
        for (int i = o; i < o + l; i++)
            d += data[i];
        int n = _max;
        if (n == 2 || n == 4 || n == 8)
            return (d ^ (d>>4)) & (n-1);
        else
            return (d % n);
    }
}

void
HashSwitch::push(int port, Packet *p)
{
    output(process(p)).push(p);
}

#if HAVE_BATCH
void
HashSwitch::push_batch(int port, PacketBatch *batch)
{
    auto fnt = [this, port](Packet *p) { return process(p); };
    CLASSIFY_EACH_PACKET(_max + 1, fnt, batch, checked_output_push_batch);
}
#endif

CLICK_ENDDECLS
EXPORT_ELEMENT(HashSwitch)
ELEMENT_MT_SAFE(HashSwitch)
