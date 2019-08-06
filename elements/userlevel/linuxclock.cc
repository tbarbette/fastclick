/*
 * linuxclock.{cc,hh} -- User Clock wrapper for Linux
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
#include "linuxclock.hh"
#include <click/glue.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <time.h>
CLICK_DECLS

LinuxClock::LinuxClock() :
_verbose(1), _install(true)
{

}

int
LinuxClock::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
            .read("VERBOSE", _verbose)
            .read("INSTALL", _install)
            .complete() < 0)
        return -1;
    return 0;
}

void *
LinuxClock::cast(const char *name) {
    if (strcmp(name,"UserClock") == 0)
        return static_cast<UserClock*>(this);
    return Element::cast(name);
}

int64_t
LinuxClock::now(bool steady) {
    struct timespec tsp;
    if (steady)
        clock_gettime(CLOCK_MONOTONIC, &tsp);
    else
        clock_gettime(CLOCK_REALTIME, &tsp);
    return (int64_t)tsp.tv_sec * 1000000000 + tsp.tv_nsec;
}

int
LinuxClock::initialize(ErrorHandler* errh) {
    return 0;
}

void LinuxClock::cleanup(CleanupStage) {
}

String
LinuxClock::read_handler(Element *e, void *thunk) {
    LinuxClock *c = (LinuxClock *)e;
    switch ((intptr_t)thunk) {
        case h_now:
            return String(c->now(false));
        case h_now_steady:
            return String(c->now(true));
        default:
            return "<error>";
    }
}

void LinuxClock::add_handlers() {
    add_read_handler("now", read_handler, h_now);
    add_read_handler("now_steady", read_handler, h_now_steady);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(usertiming)
EXPORT_ELEMENT(LinuxClock)
ELEMENT_MT_SAFE(LinuxClock)
