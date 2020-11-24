// -*- c-basic-offset: 4 -*-
/*
 * rrsched.{cc,hh} -- round robin scheduler element
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
#include <click/error.hh>
#include <click/args.hh>
#include "rrmultisched.hh"
CLICK_DECLS

RRMultiSched::RRMultiSched()
    : _n(1), _n_cur(0)
{
}

int RRMultiSched::configure(Vector<String> &conf, ErrorHandler *errh)
{
    _max = ninputs();
    if (Args(conf, this, errh)
        .read_or_set_p("N", _n, 1)
        .read_or_set_p("MAX", _max, ninputs())
        .complete() < 0)
        return -1;

    return 0;
}

Packet *
RRMultiSched::pull(int)
{
    int i = _next;
    for (int j = 0; j < _max; j++) {
        Packet *p = (_signals[i] ? input(i).pull() : 0);
        if (p) {
            _n_cur++;
            if (_n_cur == _n) {
                i++;
                if (i >= _max) {
                    i = 0;
                }
                _n_cur = 0;
            }
            _next = i;
            return p;
        } else {
            i++;
            if (i >= _max) {
                i = 0;
            }
            _n_cur = 0;
        }
    }
    return 0;
}


#if HAVE_BATCH
PacketBatch *
RRMultiSched::pull_batch(int port, unsigned max) {
    PacketBatch *batch;
    MAKE_BATCH(RRMultiSched::pull(port), batch, max);
    return batch;
}
#endif

CLICK_ENDDECLS
EXPORT_ELEMENT(RRMultiSched)
