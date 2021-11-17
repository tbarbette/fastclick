// -*- c-basic-offset: 4 -*-
/*
 * pad.{cc,hh} -- extends packet length
 * Eddie Kohler, Tom Barbette
 *
 * Copyright (c) 2004 Regents of the University of California
 * Copyright (c) 2019 KTH Royal Institute of Technology
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
#include "pad.hh"
#include <click/packet_anno.hh>
#include <click/args.hh>
#include <click/error.hh>
CLICK_DECLS

Pad::Pad()
{
}

int
Pad::configure(Vector<String>& conf, ErrorHandler* errh)
{
    _nbytes = 0;
    _maxlength = 0;
    _zero = true;
    _random = false;
    _verbose = false;
    return Args(conf, this, errh)
        .read_p("LENGTH", _nbytes)
        .read("ZERO", _zero)
        .read("RANDOM", _random)
        .read("MAXLENGTH", _maxlength)
        .read("VERBOSE", _verbose)
        .complete();

    if (_zero && _random)
        return errh->error("ZERO and RANDOM are exclusive.");

    return 0;
}

Packet*
Pad::simple_action(Packet* p)
{
    uint32_t nput;
    if (unlikely(_nbytes))
        nput = p->length() < _nbytes ? _nbytes - p->length() : 0;
    else
        nput = EXTRA_LENGTH_ANNO(p);
    if (unlikely(_maxlength) && unlikely(_maxlength < (nput + p->length())))
    {
        if (unlikely(_verbose))
            click_chatter("Tried a too long Pad: %i + %i > %i -> adding only %i bytes",
                    p->length(), nput, _maxlength, _maxlength - p->length());
         nput = _maxlength - p->length();
    }
    if (nput) {
        WritablePacket* q = p->put(nput);
        if (!q)
            return 0;
        if (_zero)
            memset(q->end_data() - nput, 0, nput);
        if (_random) {
            unsigned char* data = q->end_data() - nput;
            for (unsigned i = 0; i < nput / 4; i++,data+=4) {
                *((uint32_t*)data) = click_random();

            }
        }

        p = q;
    }
    SET_EXTRA_LENGTH_ANNO(p, 0);
    return p;
}

CLICK_ENDDECLS

EXPORT_ELEMENT(Pad)
ELEMENT_MT_SAFE(Pad)

