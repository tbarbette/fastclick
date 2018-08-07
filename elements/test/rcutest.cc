// -*- c-basic-offset: 4 -*-
/*
 * rcutest.{cc,hh} -- regression test element for click_rcu
 * Tom Barbette
 *
 * Copyright (c) 2016 Cisco Meraki
 * Copyright (c) 2016 University of Liege
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
#include "rcutest.hh"
#include <click/glue.hh>
#include <click/error.hh>
#include <click/args.hh>
#include <click/master.hh>
CLICK_DECLS

RCUTest::RCUTest()
    : _rcu(nastruct{0,0}), _fast_rcu(nastruct{0,0}), _nruns(0)
{
}

bool
RCUTest::run_task(Task *t)
{
    if (*_nruns & 1) {
        nastruct &n = _rcu.write_begin();
        ++n.a;
        ++n.b;
        _rcu.write_commit();
        int flags;
        nastruct &fast_n = _fast_rcu.write_begin(flags);
        ++fast_n.a;
        ++fast_n.b;
        _fast_rcu.write_commit(flags);
    } else {
        int flags;
        const nastruct &n = _rcu.read_begin(flags);
        assert(n.a == n.b);
        _rcu.read_end(flags);
        const nastruct &fast_n = _fast_rcu.read_begin(flags);
        assert(fast_n.a == fast_n.b);
        _fast_rcu.read_end(flags);
    }

    _nruns++;
    if (*_nruns < 10000) {
        t->fast_reschedule();
    } else {
        router()->please_stop_driver();
    }
    return true;
}

String
RCUTest::read_param(Element *e, void *thunk_p)
{
    RCUTest *vc = (RCUTest *)e;

    uintptr_t thunk = (uintptr_t) thunk_p;

    switch (thunk) {
      case 0: {
        return String(vc->_rcu.read().a);
      }
      case 1: {
        return String(vc->_fast_rcu.read().a);
      }
      case 2: {
        return String(per_thread_arithmetic::sum(vc->_nruns));
      }
    }
    return String::make_empty();
}

void RCUTest::add_handlers() {
    add_read_handler("rcu", read_param, 0);
    add_read_handler("fast_rcu", read_param, 1);
    add_read_handler("status", read_param, 2);
}


CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel)
EXPORT_ELEMENT(RCUTest)
