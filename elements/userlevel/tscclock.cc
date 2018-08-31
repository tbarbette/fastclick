/*
 * tscclock.{cc,hh} -- tsc-based clock
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
#include "tscclock.hh"
#include <click/glue.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/master.hh>
CLICK_DECLS

TSCClock::TSCClock() :
_verbose(1), _install(true), _nowait(false), _allow_offset(false),_correction_timer(this), _sync_timers(0), _base(0)
{

}

void *
TSCClock::cast(const char *name) {
    return Element::cast(name);
}

int
TSCClock::configure(Vector<String> &conf, ErrorHandler *errh)
{
#if HAVE_DPDK
    _allow_offset = true;
#endif
    Element* basee = 0;
    if (Args(conf, this, errh)
            .read("VERBOSE", _verbose)
            .read("INSTALL", _install)
            .read("NOWAIT", _nowait)
            .read("BASE",basee)
            .complete() < 0)
        return -1;

    if (basee)
        if (!(_base = static_cast<UserClock*>(basee->cast("UserClock"))))
            return errh->error("%p{element} is not a UserClock",basee);

    if (_nowait && !_install)
        return errh->error("You want to install without waiting but do not want to install?!");
    return 0;
}

int64_t TSCClock::now(void* user, bool steady) {
    if (steady)
        return reinterpret_cast<TSCClock*>(user)->compute_now_steady();
    else
        return reinterpret_cast<TSCClock*>(user)->compute_now_wall();
}

/**
 * Return real current time
 */
inline int64_t TSCClock::get_real_timestamp(bool steady) {
    Timestamp t;
    if (_base) {
        return _base->now(steady);
    } else {
        t.assign_now_nouser(steady);
        return t.longval();
    }
}

/*
 * Basically a clock is composed of :
 *  - A base timestamp, indicating the time of the last tick
 *  - The TSC frequency, so the current time is last_timestamp + delta_tsc / freq
 *    but it would be slow to do a 64bit division each time, so it is decomposed into :
 *    - A multiplication factor
 *    - A shift factor
 *    So mult>>shift ~= freq
 *
 * To mimic an atomic change of those variables, we use a ring of 2 clocks. This function write
 * the next frequency and bqse timestamp into the second clock and then updates the current
 * clock index. If someone was in the middle of computing the time, it will read the three variable
 * from the last clock before going to the other thanks to a read memory fence.
 */
int
TSCClock::initialize(ErrorHandler*) {
    tsc_freq = cycles_hz();

    int64_t mult;
    cycles_per_subsec_shift = 1;

    while (1) {
        mult = freq_to_mult(tsc_freq);
        /*We have 64 bit to represent a nanosecond time, the bigger the mult
         * is, the better it is. We update the time about every tenth of
         * seconds, so
         * let it be 2 just to be sure. So the delta could be as high as
         * 0.2*10^9, that being 2^32, so we want a mult below 2^(63-32)=2^31.
         */
        if (mult < INT_MAX / 2) {
            cycles_per_subsec_shift += 1;
        } else {
            break;
        }
    }
    cycles_per_subsec_mult[0] = mult;
    cycles_per_subsec_mult[1] = mult;

    //Initialize real clock
    last_timestamp[0] = get_real_timestamp();
    last_cycles[0] = click_get_cycles();

    max_precision = 100 * (double)Timestamp::subsec_per_sec/(double)tsc_freq;
    //Need to converge quickly enough, but have a period large enough so that the cycle is still correct
    update_period_subsec = (int64_t)Timestamp::subsec_per_sec / 10;
    update_period_msec = update_period_subsec / (Timestamp::subsec_per_sec / Timestamp::msec_per_sec);
    _correction_timer.initialize(this);
    _correction_timer.schedule_now();

    if (_install && _nowait)
        Timestamp::set_clock(&now,(void*)this);

    return 0;
}

/**
 * Stabilization phase
 * Try to get the real clock freauency by adding some percentage of the last
 * measured frequency to the base value until the final value is good enough.
 */
bool TSCClock::stabilize_tick() {
    int64_t cur_cycles = click_get_cycles();
    int64_t real_current_timestamp = get_real_timestamp();

    //Number of ticks during last period
    int64_t delta_tick = cur_cycles - last_cycles[current_clock];

    //Current time based on the real time of last period tick
    int64_t base_current_timestamp = last_timestamp[current_clock] + tick_to_subsec_wall(delta_tick);

    //Last period error
    int64_t period_error_delta_timestamp = real_current_timestamp - base_current_timestamp;

    //Observed frequency since last tick
    double real_freq = delta_to_freq(delta_tick,real_current_timestamp - last_timestamp[current_clock]);

    //Correct TSC frequency
    tsc_freq = (int64_t)((double)tsc_freq * (1 - alpha) + alpha * real_freq);

    //Lower alpha if we get a good precision 10 times in a row
    if (abs(period_error_delta_timestamp) < max_precision) {
        alpha_stable++;
        if (alpha_stable == 10) {
            alpha /= 10;
            alpha_stable = 0;
            //Stop when alpha cannot change the hz anymore
            if (alpha * (double)tsc_freq < 1) {
                return true;
            }
        }
    } else {
        alpha_stable = 0;
        if (alpha_unstable++ == 10) {
            //Restart the process
            alpha = 0.5;
        }
    }

    if (_verbose > 2) {
        click_chatter("Phase: STABILIZING\nTSC freq : %f\nLast period error : %ld\nAccumulation factor : %f",
                      tsc_freq,
                      period_error_delta_timestamp,
                      alpha);
    }

    cycles_per_subsec_mult[current_clock] = freq_to_mult(tsc_freq);
    last_timestamp[current_clock] = real_current_timestamp;
    last_cycles[current_clock] = cur_cycles;
    return false;
}

/**
 * Accumulate the ticks from the last period into the base timestamp.
 * Also, change the wall clock frequency for the next period to catch any drift.
 */
bool TSCClock::accumulate_tick(Timer* t) {
    double catchup_freq;
    double next_tsc_freq;

    unsigned next_clock = (current_clock == 0? 1 : 0);

    steady_cycle[next_clock] = click_get_cycles();
    steady_timestamp[next_clock] = steady_timestamp[current_clock] + tick_to_subsec_steady(steady_cycle[next_clock] - steady_cycle[current_clock]);
    //Check that the timer period was not too high, as this could cause TSC computation overflow in tick_to_subsec
    if ((steady_timestamp[next_clock] - steady_timestamp[current_clock]) > (update_period_subsec * 2)) {
        //We try all the click threads
        int nt = (t->home_thread_id() + 1) % master()->nthreads();
        //If we did a full loop, we disable the clock...
        if (nt == home_thread()->thread_id()) {
            if (_verbose)
                click_chatter("Click tasks are too heavy and the TSC clock cannot run at least once every %dmsec, the TSC clock is deactivated.");
            if (_install)
                Timestamp::set_clock(0,0);
            return false;
        }
        t->move_thread(nt);
        if (_verbose > 1)
            click_chatter("Thread %d is doing too much work and prevent having a good clock, trying the next one");
    }

    //We want to always take current cycle as close as possible to real timestamp, so we read it again just before get real timestamp
    int64_t cur_cycles = click_get_cycles();
    int64_t real_current_timestamp = get_real_timestamp();

    int64_t delta_tick = cur_cycles - last_cycles[current_clock];

    //Approximate time since last period tick, using the last period frequency. Any call to now() would resolve to this delta
    int64_t approx_delta_timestamp = tick_to_subsec_wall(delta_tick);

    //Current time based on the computed time of last tick
    int64_t approx_current_timestamp = last_timestamp[current_clock] + approx_delta_timestamp;

    //Total accumulated error
    int64_t total_error_delta_timestamp = real_current_timestamp - approx_current_timestamp;

    //If the total accumulated error needs too much catch up, let's jump
    if (total_error_delta_timestamp > Timestamp::subsec_per_sec || total_error_delta_timestamp < -Timestamp::subsec_per_sec) {
        click_chatter("Date has shifted by %ld sec, userlevel clock will jump to catch up",total_error_delta_timestamp / Timestamp::subsec_per_sec);

        last_timestamp[next_clock] = real_current_timestamp;
        cycles_per_subsec_mult[next_clock] = cycles_per_subsec_mult[current_clock];
        last_cycles[next_clock] = cur_cycles;

        goto fence;
    }

    //Frequency which would have allowed to catch up on the last period
    catchup_freq = delta_to_freq(delta_tick,real_current_timestamp - last_timestamp[current_clock]);

    last_timestamp[next_clock] = approx_current_timestamp;
    next_tsc_freq = tsc_freq * (1 - alpha) + catchup_freq * alpha;

    total_error += abs(total_error_delta_timestamp);
    avg_freq += next_tsc_freq;
    n_ticks ++;

    if (_verbose > 2) {
        click_chatter("Phase: RUNNING\nTSC freq : %f\nNext TSC freq : %f\nAccumulated error : %ld\n",
                      tsc_freq,
                      next_tsc_freq,
                      total_error_delta_timestamp);
    }

    cycles_per_subsec_mult[next_clock] = freq_to_mult(next_tsc_freq);

    /*A big amount of time could have passed since the beginning of this
     * function, to prevent jump we recompute the current approximated
     * time again and use that as a basis for next new frequency
     * multiplication.*/
    last_cycles[next_clock] = click_get_cycles();
    last_timestamp[next_clock] = last_timestamp[current_clock] + tick_to_subsec_wall(last_cycles[next_clock] - last_cycles[current_clock]);

    //Swap clock
    fence:
    click_write_fence();
    current_clock = next_clock;

    return true;
}

/**
 * Per-core synchronization timer
 * This runs on all cores after stabilization to check if all the TSC are in sync, and if other cores are
 * able to get the right time
 */
void TSCClock::run_sync_timer(Timer* t, void* user) {
    TSCClock* tc = reinterpret_cast<TSCClock*>(user);

    //Get a local copy of the current clock so we stay on the same base
    int64_t local_current_clock = tc->current_clock;
    click_read_fence();

    int64_t real_time = tc->get_real_timestamp();
    int64_t approx_time = tc->compute_now_wall(local_current_clock);
    int64_t delta = real_time - approx_time;
    if (abs(delta) > tc->max_precision) {
        tc->tstate->local_tsc_offset += tc->subsec_to_tick(delta,tc->cycles_per_subsec_mult[local_current_clock]);
        if (tc->tstate->local_synchronize_bad++ == 10) {
            if (tc->_verbose > 1)
                click_chatter("%s : TSC clock of core %d is not synchronized... It drifted by %ld subsecs.",
                                    tc->name().c_str(),
                                    click_current_cpu_id(),
                                    delta);
            tc->_synchronize_bad++;
            return;
        }
    } else {
        if (tc->tstate->local_synchronize_bad-- == -10) {
            //Do not set an offset if it is small as this is probably a computation error
            if (tc->tstate->local_tsc_offset < 50 && tc->tstate->local_tsc_offset > -50)
                tc->tstate->local_tsc_offset = 0;
            else {
                if (!tc->_allow_offset) {
                    tc->_synchronize_bad++;
                    if (tc->_verbose > 1)
                        click_chatter("%s : TSC clock on thread %d needs an offset of %ld, "
                                      "but ALLOW_OFFSET is not true.",
                                            tc->name().c_str(),
                                            click_current_cpu_id(),
                                            tc->tstate->local_tsc_offset);
                    return;
                }
            }
            tc->_synchronize_ok++;
            if (tc->_verbose > 1)
                click_chatter("%s : TSC clock of core %d IS synchronized : local offset is %ld",
                                    tc->name().c_str(),
                                    click_current_cpu_id(),
                                    tc->tstate->local_tsc_offset);
            return;
        }
    }

    t->schedule_after_msec(tc->update_period_msec);
}


/**
 * Main timer in charge of choosing the right phase sub-fonction and switching between phases
 */
void TSCClock::run_timer(Timer* timer) {
    if (unlikely(_phase == STABILIZE)) {
        if (stabilize_tick()) {
            //Initialize steady clock
            steady_timestamp[current_clock] = get_real_timestamp(true);
            steady_cycle[current_clock] = click_get_cycles();
            steady_cycles_per_subsec_mult = cycles_per_subsec_mult[current_clock];

            //Initialize wall clock
            last_cycles[current_clock] = click_get_cycles();
            last_timestamp[current_clock] = get_real_timestamp(false);

            alpha = 0.5;
            _phase = SYNCHRONIZE;
        }
    } else {
        if (unlikely(!accumulate_tick(timer)))
            return;
    }

    if (unlikely(_phase == SYNCHRONIZE)) {
        //Launch sync taks
        if (_sync_timers == 0 && n_ticks == 10) {
            _sync_timers = new Timer*[master()->nthreads()];
             _synchronize_bad = 0;
             _synchronize_ok = 0;
            for (int i = 0; i < master()->nthreads(); i++) {
                _sync_timers[i] = new Timer(&run_sync_timer, this);
                _sync_timers[i]->initialize(this);
                _sync_timers[i]->move_thread(i);
                _sync_timers[i]->schedule_after_msec(update_period_msec / 2);
            }
        }
        if (n_ticks > 10) {
            if (_synchronize_bad > 0) {
                //Abandon ship
                if (_verbose)
                    click_chatter("TSC between core do not seems to be able to synchronize... The TSC clock cannot be used. Sorry.");
                return;
            }

            if (_synchronize_ok == (unsigned)master()->nthreads()) {
                //Start using the clock !
                _phase = RUNNING;
                if (_verbose)
                    click_chatter("Switching to TSC clock");
                if (_install && !_nowait)
                    Timestamp::set_clock(&now,(void*)this);
            }
        }
    }

    timer->schedule_after_msec(update_period_msec);
}

void TSCClock::cleanup(CleanupStage) {
    if (_sync_timers) {
        for (int i = 0; i < master()->nthreads(); i++)
            delete _sync_timers[i];
        delete[] _sync_timers;
    }
}

String
TSCClock::read_handler(Element *e, void *thunk) {
    TSCClock *c = (TSCClock *)e;
    switch ((intptr_t)thunk) {
        case h_now:
            return String(c->compute_now_wall());
        case h_now_steady:
            return String(c->compute_now_steady());
        case h_cycles:
            return String(click_get_cycles() + c->tstate->local_tsc_offset);
        case h_cycles_hz:
            return String(c->tsc_freq);
        case h_phase:
            return String(c->_phase);
        default:
            return "<error>";
    }
}

void TSCClock::add_handlers() {
    add_read_handler("now", read_handler, h_now);
    add_read_handler("now_steady", read_handler, h_now_steady);
    add_read_handler("cycles", read_handler, h_cycles);
    add_read_handler("cycles_hz", read_handler, h_cycles_hz);
    add_read_handler("phase", read_handler, h_phase);
}


CLICK_ENDDECLS
ELEMENT_REQUIRES(usertiming)
EXPORT_ELEMENT(TSCClock)
ELEMENT_MT_SAFE(TSCClock)
