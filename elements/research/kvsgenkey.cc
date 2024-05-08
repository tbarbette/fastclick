// -*- c-basic-offset: 4 -*-
/*
 * KVSKeyGen.{cc,hh} --
 * Tom Barbette
 *
 * Copyright (c) 2017 University of Liege
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
#include "kvsgenkey.hh"
#include <click/args.hh>
#include <click/error.hh>
#include <click/standard/scheduleinfo.hh>

#define FRAND_MAX _gens->max()

CLICK_DECLS

std::random_device KVSKeyGen::rd;


KVSKeyGen::KVSKeyGen()
{
}

int
KVSKeyGen::configure(Vector<String> &conf, ErrorHandler *errh)
{
    int s;
    if (Args(conf, this, errh)
        .read_or_set("OFFSET", _offset, 0)
        .complete() < 0)
        return -1;


    for (unsigned i = 0; i < _gens.weight(); i ++) {
        _gens.set_value_for_thread(i, std::mt19937{rd()});
    }

    return 0;
}

Packet*
KVSKeyGen::simple_action(Packet* p_in)
{
    WritablePacket* p = p_in->uniqueify();

    unsigned* data = (unsigned*)((unsigned char*)( p->udp_header() + 1) + _offset);

    *data = (*_gens)();

    return p;
}


CLICK_ENDDECLS
EXPORT_ELEMENT(KVSKeyGen)
