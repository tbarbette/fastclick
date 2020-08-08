/*
 * randload.{cc,hh} -- Element artificially creates CPU burden
 *
 * Tom Barbette
 *
 * Copyright (c) 2020 KTH Royal Institute of Technology
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

#include "randload.hh"

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>

CLICK_DECLS

RandLoad::RandLoad() {

};

RandLoad::~RandLoad() {

}

int
RandLoad::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
               .read_or_set("MIN", _min, 1)
               .read_or_set("MAX", _max, 100)
               .complete() < 0)
        return -1;

    return 0;
}


int RandLoad::initialize(ErrorHandler *errh) {
    return 0;
}

void RandLoad::push_batch(int port, PacketBatch* batch) {
    int r;
    auto fnt = [this,&r](Packet* p)  {
        int w =  _min + ((*_gens)() / (UINT_MAX / (_max - _min) ));
        for (int i = 0; i < w - 1; i ++) {
            r = (*_gens)();
        }
        return p;
    };
    EXECUTE_FOR_EACH_PACKET(fnt, batch);

    output_push_batch(0, batch);
}


CLICK_ENDDECLS

ELEMENT_REQUIRES(batch)
EXPORT_ELEMENT(RandLoad)
ELEMENT_MT_SAFE(RandLoad)
