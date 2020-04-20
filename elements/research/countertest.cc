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
#include <click/standard/scheduleinfo.hh>

CLICK_DECLS

CounterTest::CounterTest() : _counter(0), _rate(0), _atomic(true), _standalone(false), _task(this), _read(0), _write(0), _pass(1)
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
        .read("STANDALONE", _standalone)
        .read("PASS", _pass)
        .complete() < 0)
        return -1;
    _counter = (CounterBase*)e->cast("CounterBase");
    if (!_counter)
        return errh->error("%p{element} is not a counter !",e);
    if (_standalone)
        ScheduleInfo::initialize_task(this, &_task, errh);
#if defined(__GNUC__) && !defined(__clang__)
    void (CounterBase::*aaddfnt)(CounterBase::stats) = &CounterBase::atomic_add;
    void (CounterBase::*addfnt)(CounterBase::stats) = &CounterBase::add;
    CounterBase::stats (CounterBase::*areadfnt)() = &CounterBase::atomic_read;
    CounterBase::stats (CounterBase::*readfnt)() = &CounterBase::read;
    if (_atomic) {
        _add_fnt=(void (*)(CounterBase*,CounterBase::stats))(_counter->*aaddfnt);
        _read_fnt=(CounterBase::stats(*)(CounterBase*))(_counter->*areadfnt);
    } else {
        _add_fnt=(void (*)(CounterBase*,CounterBase::stats))(_counter->*addfnt);
        _read_fnt=(CounterBase::stats(*)(CounterBase*))(_counter->*readfnt);
    }
#endif
    if (_pass < 1)
        return errh->error("PASS must be >= 1");
    return 0;
}

#if HAVE_BATCH
void
CounterTest::push_batch(int, PacketBatch* batch)
{
    if (++_cur_pass == _pass) {
        _cur_pass.set(0);
        for (int i = 0; i < _rate; i++) {
            if (!router()->running())
                break;
            if (_atomic) {
#if defined(__GNUC__) && !defined(__clang__)
            _read_fnt(_counter);
#else
            _counter->atomic_read();
#endif
            } else {
                _counter->count();
            }
        }
    }
    output_push_batch(0, batch);
}
#endif

void
CounterTest::push(int, Packet* p)
{
    for (int i = 0; i < _rate; i++) {
        if (master()->paused())
            break;
#if defined(__GNUC__) && !defined(__clang__)
        _read_fnt(_counter);
#else
        if (_atomic)
            _counter->atomic_read();
        else
            _counter->read();
#endif
    }
    output_push(0, p);
}

bool
CounterTest::run_task(Task* t)
{
    for (int i = 0; i < _rate; i++) {
        if (master()->paused())
            break;
#if defined(__GNUC__) && !defined(__clang__)
        _read_fnt(_counter);
#else
        if (_atomic)
            _counter->atomic_read();
        else
            _counter->read();
#endif
        _read++;
    }
#if defined(__GNUC__) && !defined(__clang__)
    _add_fnt(_counter,{1,1});
#else
    if (_atomic)
        _counter->atomic_add({1,1});
    else
        _counter->add({1,1});
#endif
    _write++;
    t->fast_reschedule();
    return true;
}

void
CounterTest::add_handlers()
{
    add_data_handlers("read", Handler::f_read, &_read);
    add_data_handlers("write", Handler::f_read, &_write);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(CounterTest)
