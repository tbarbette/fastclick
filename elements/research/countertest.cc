// -*- c-basic-offset: 4 -*-
/*
 * countertest.{cc,hh} --
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
#include "countertest.hh"
#include <click/args.hh>
#include <click/error.hh>
#include "../standard/counter.hh"

CLICK_DECLS

CounterTest::CounterTest() : _counter(0), _rate(0), _atomic(true)
{
}

int
CounterTest::configure(Vector<String> &conf, ErrorHandler *errh)
{
    Element* e;

    if (Args(conf, this, errh)
        .read_mp("COUNTER", e)
        .read_mp("RATE", _rate)
        .read("ATOMIC", _atomic)
        .complete() < 0)
        return -1;
    _counter = (CounterBase*)e->cast("CounterBase");
    if (!_counter)
        return errh->error("%p{element} is not a counter !",e);
    return 0;
}

void
CounterTest::push_batch(int, PacketBatch* batch) {
    for (int i = 0; i < _rate; i++) {
        if (_atomic) {
            _counter->atomic_read();
        } else {
            _counter->count();
        }
    }
    output_push_batch(0, batch);
}

/*
void
CounterTest::run_task(int port,Packet* p)
{
    c
}*/



CLICK_ENDDECLS
EXPORT_ELEMENT(CounterTest)
