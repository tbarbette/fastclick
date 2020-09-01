/*
 * flowrandload.{cc,hh} -- Element artificially creates CPU burden per flow
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

#include "flowrandload.hh"

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include <click/flow/flow.hh>

CLICK_DECLS

FlowRandLoad::FlowRandLoad() {

};

FlowRandLoad::~FlowRandLoad() {

}

int
FlowRandLoad::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
               .read_or_set("MIN", _min, 1)
               .read_or_set("MAX", _max, 100)
               .complete() < 0)
        return -1;

    return 0;
}


int FlowRandLoad::initialize(ErrorHandler *errh) {
    return 0;
}

void FlowRandLoad::push_flow(int port, RandLoadState* flowdata, PacketBatch* batch) {
    if (flowdata->w == 0) {
        flowdata->w =  _min + ((*_gens)() / (UINT_MAX / (_max - _min) ));  //click_random(_min, _max);
    }
    int r;
    auto fnt = [this,flowdata,&r](Packet* p) {
        for (int i = 0; i < flowdata->w; i ++) {
            r = (*_gens)();
        }
        return p;
    };
    EXECUTE_FOR_EACH_PACKET(fnt, batch);

    output_push_batch(0, batch);
}


CLICK_ENDDECLS

ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(FlowRandLoad)
ELEMENT_MT_SAFE(FlowRandLoad)
