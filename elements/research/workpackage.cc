// -*- c-basic-offset: 4 -*-
/*
 * WorkPackage.{cc,hh} --
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
#include "workpackage.hh"
#include <click/args.hh>
#include <click/error.hh>
#include <click/standard/scheduleinfo.hh>

#define FRAND_MAX _gens->max()

CLICK_DECLS

std::random_device WorkPackage::rd;


WorkPackage::WorkPackage() : _w(1)
{
}

int
WorkPackage::configure(Vector<String> &conf, ErrorHandler *errh)
{
    double _s;
    if (Args(conf, this, errh)
        .read_or_set("S", _s, 0) //Array size in MB
        .read_or_set("N", _n, 0) //Number of array access (4bytes)
        .read_or_set("R", _r, 0) //Percentage of access that are packet read (vs Array access) between 0 and 100
        .read_or_set("PAYLOAD", _payload, false) //Access payload or only header
        .read_or_set("W",_w, 0) //Amount of call to random, purely CPU intensive fct
        .complete() < 0)
        return -1;
    _array.resize(int(_s * 1024 * 1024 / (float)sizeof(uint32_t)));
    for (int i = 0; i < _array.size(); i++) {
        _array[i] = rd();
    }
    for (int i = 0; i < _gens.weight(); i ++) {
        _gens.set_value_for_thread(i, std::mt19937{rd()});
    }
    return 0;
}

void
WorkPackage::rmaction(Packet* p, int &n_data)
{
    uint32_t sum = 0;
    unsigned r = 0;
    for (int i = 0; i < _w; i ++) {
        r = (*_gens)();
    }
    for (int i = 0; i < _n; i++) {
        uint32_t data;
        if (r / (FRAND_MAX / 100 + 1) < _r) {
            int pos = r / (FRAND_MAX / ((_payload?p->length():54) + 1) + 1);
            data = *(uint32_t*)(p->data() + pos);
            //n_data++;
        } else {
            if (_array.size() > 0) {
                unsigned pos = r / ((FRAND_MAX / (_array.size() + 1)) + 1);
                data = _array[pos];
            }
        }
        r = data ^ (r << 24 ^ r << 16  ^ r << 8 ^ r >> 16);
    }
}

#if HAVE_BATCH
void
WorkPackage::push_batch(int port, PacketBatch* batch)
{
    int n_data = 0;
    FOR_EACH_PACKET(batch, p)
            rmaction(p,n_data);
    output_push_batch(port, batch);
}
#endif

void
WorkPackage::push(int port, Packet* p)
{
    int n_data = 0;
    rmaction(p,n_data);
    output_push(port, p);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(WorkPackage)
