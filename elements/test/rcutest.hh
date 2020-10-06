// -*- c-basic-offset: 4 -*-
#ifndef CLICK_RCUTEST_HH
#define CLICK_RCUTEST_HH
#include <click/element.hh>
#include <click/task.hh>
#include <click/multithread.hh>
#include "mtdietest.hh"

CLICK_DECLS

/*
=c

RCUTest([I<keywords>])

=s test

runs regression tests for click_rcu

*/

class RCUTest : public MTDieTest { public:

    RCUTest() CLICK_COLD;

    const char *class_name() const override		{ return "RCUTest"; }

    static String read_param(Element *e, void *thunk_p);
    void add_handlers();
    bool run_task(Task *t);

  private:
    struct nastruct {
        int64_t a;
        int64_t b;
    };

    click_rcu<nastruct> _rcu;
    fast_rcu<nastruct> _fast_rcu;
    per_thread<int> _nruns;
};

CLICK_ENDDECLS
#endif
