/*
 * jiffieclock.{cc,hh} -- accumulating jiffie clock
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
#include "jiffieclock.hh"
#include <click/glue.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/master.hh>
CLICK_DECLS

JiffieClock::JiffieClock() :
_task(this), _timer(this), _verbose(false),_minprecision(2)
{

}

int
JiffieClock::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
        .read_p("VERBOSE", _verbose)
        .read_p("MINPRECISION", _minprecision)
        .complete() < 0)
        return -1;
    return 0;
}

/**
 * Return real current time
 */
inline int64_t get_real_timestamp(bool steady = false) {
    Timestamp t;
    t.assign_now_nouser(steady);
    return t.longval();
}

int
JiffieClock::initialize(ErrorHandler*) {
    /*The jiffie is not set in this initialization function, as after
     * this we could TODO*/
    _task.initialize(this,true);
    _timer.initialize(this);
    return 0;
}

void JiffieClock::run_timer(Timer* t) {
    Timestamp current_time = Timestamp::now_steady();
    int64_t delta = (current_time - last_jiffies_update).msec() / (1000 / CLICK_HZ);
    if (delta > 0) {
        if (unlikely(delta > _minprecision)) {//Accept a little jump from time to time, but not double jump
            //We try all the click threads
            int nt = (t->home_thread_id() + 1) % master()->nthreads();
            if (nt == home_thread_id()) {
                click_chatter("Click tasks are too heavy and the jiffie accumulator cannot run at least once every %dmsec, the user jiffie clock is deactivated.",_minprecision);
                click_jiffies_fct = &click_timestamp_jiffies;
                return;
            }
            if (_verbose)
                click_chatter("%p{element}: Thread %d is doing too much work and prevent accumulating jiffies accurately (this tick accumulated %d jiffies), trying the next one",this,t->home_thread_id(),delta);
            t->move_thread(nt);
        }
        jiffies += delta;
        last_jiffies_update = current_time;
    }
    t->schedule_at_steady(last_jiffies_update + Timestamp::make_msec(1000 / CLICK_HZ));
}

click_jiffies_t JiffieClock::read_jiffies(void* data) {
    return static_cast<JiffieClock*>(data)->jiffies;
}


bool JiffieClock::run_task(Task*) {
    if (_verbose)
        click_chatter("Switching to accumulated jiffies");
    jiffies = click_jiffies();
    last_jiffies_update = Timestamp::now_steady();
    //TODO : this should be rcu protected
    click_jiffies_fct_data = this;
    click_jiffies_fct = &read_jiffies;
    _timer.schedule_after_msec(1000 / CLICK_HZ);
    return true;
}

String
JiffieClock::read_param(Element *e, void *thunk)
{
  switch (reinterpret_cast<intptr_t>(thunk)) {
    case 0:
        return String(click_jiffies());
    case 1:
        return String(click_jiffies_fct == &read_jiffies);
  }
  return "";
}

void
JiffieClock::add_handlers()
{
    add_read_handler("jiffies", read_param, 0);
    add_read_handler("status", read_param, 1);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(usertiming)
EXPORT_ELEMENT(JiffieClock)
ELEMENT_MT_SAFE(JiffieClock)
