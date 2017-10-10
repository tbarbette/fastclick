/*
 * mbgclock.{cc,hh} -- Meinberg-based clock
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
#include "mbgclock.hh"
#include <click/glue.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/master.hh>
CLICK_DECLS

MBGClock::MBGClock() :
_verbose(1), _install(true), _comp(true)
{

}

void *
MBGClock::cast(const char *name) {
    if (strcmp(name,"UserClock") == 0)
        return static_cast<UserClock*>(this);
    return Element::cast(name);
}


int
MBGClock::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
            .read("VERBOSE", _verbose)
            .read("INSTALL", _install)
            .read("COMP", _comp)
            .complete() < 0)
        return -1;
    return 0;
}

int64_t
MBGClock::now(bool) {
    PCPS_TIME_STAMP ts;

    if (!_comp)
        mbg_get_fast_hr_timestamp(_dh, &ts);
    else
        mbg_get_fast_hr_timestamp_comp(_dh, &ts, 0 );
    return ((int64_t)ts.sec * 1000000000) + (int64_t)(bin_frac_32_to_nsec(ts.frac)) ;
}

uint64_t
MBGClock::cycles() {
    PCPS_TIME_STAMP_CYCLES ts;
    mbg_get_fast_hr_timestamp_cycles(_dh, &ts );
    MBG_PC_CYCLES cycles = ts.cycles;
    return cycles;
}

int
MBGClock::initialize(ErrorHandler* errh) {
    int ret_val = 0;
    int devices = mbg_find_devices();
    if ( devices == 0 )
    {
      return errh->error( "No device found.\n");
    }
    _dh = mbg_open_device( 0 );
    //ret_val = mbg_check_device( _dh, NULL, fnc );
    return ret_val;
}

void MBGClock::cleanup(CleanupStage) {
    if (_dh)
        mbg_close_device( &_dh );
}

String
MBGClock::read_handler(Element *e, void *thunk) {
    MBGClock *c = (MBGClock *)e;
    switch ((intptr_t)thunk) {
        case h_now:
            return String(c->now());
        /*case h_now_steady:
            return String(compute_now_steady());*/
        case h_cycles:
            return String(c->cycles());
        default:
            return "<error>";
    }
}

void MBGClock::add_handlers() {
    add_read_handler("now", read_handler, h_now);
    add_read_handler("cycles", read_handler, h_cycles);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(usertiming mbglib)
EXPORT_ELEMENT(MBGClock)
ELEMENT_MT_SAFE(MBGClock)
