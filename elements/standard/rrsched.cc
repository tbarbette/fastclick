// -*- c-basic-offset: 4 -*-
/*
 * rrsched.{cc,hh} -- round robin scheduler element
 * Robert Morris, Eddie Kohler
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
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
#include <click/error.hh>
#include <click/args.hh>
#include "rrsched.hh"
CLICK_DECLS

RRSched::RRSched()
    : _next(0), _signals(0), _max(0)
{
}

int RRSched::configure(Vector<String> &conf, ErrorHandler *errh)
{
    _max = ninputs();
    if (Args(conf, this, errh)
        .read_p("MAX", _max)
        .complete() < 0)
        return -1;

    return 0;
}

int
RRSched::initialize(ErrorHandler *errh)
{
    if (!(_signals = new NotifierSignal[ninputs()]))
        return errh->error("out of memory!");
    for (int i = 0; i < ninputs(); i++) {
        _signals[i] = Notifier::upstream_empty_signal(this, i);
    }
    return 0;
}

void
RRSched::cleanup(CleanupStage)
{
    delete[] _signals;
}

Packet *
RRSched::pull(int)
{
    int i = _next;
    for (int j = 0; j < _max; j++) {
        Packet *p = (_signals[i] ? input(i).pull() : 0);

        i++;
        if (i >= _max) {
            i = 0;
        }
        if (p) {
            _next = i;
            return p;
        }
    }
    return 0;
}


#if HAVE_BATCH
PacketBatch *
RRSched::pull_batch(int port, unsigned max) {
    PacketBatch *batch;
    MAKE_BATCH(RRSched::pull(port), batch, max);
    return batch;
}
#endif

CLICK_ENDDECLS
EXPORT_ELEMENT(RRSched)
