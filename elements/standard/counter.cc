/*
 * counter.{cc,hh} -- element counts packets, measures packet rate
 * Eddie Kohler, Tom Barbette
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2008 Regents of the University of California
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
#include "counter.hh"
#include <click/error.hh>
#include <click/confparse.hh>
#include <click/args.hh>

CLICK_DECLS

CounterBase::CounterBase()
: _count_trigger_h(0), _byte_trigger_h(0), _batch_precise(false), _atomic(0),
  _simple(false)
{
}

CounterBase::~CounterBase()
{
    delete _count_trigger_h;
    delete _byte_trigger_h;
}

void*
CounterBase::cast(const char *name)
{
    if (strcmp("CounterBase", name) == 0)
        return (CounterBase *)this;
    else
        return Element::cast(name);
}

int
CounterBase::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String count_call, byte_count_call;
    bool norate = false;
    if (Args(conf, this, errh)
            .read("NO_RATE",norate)
            .read("COUNT_CALL", AnyArg(), count_call)
            .read("BYTE_COUNT_CALL", AnyArg(), byte_count_call)
            .read("BATCH_PRECISE", _batch_precise)
            .read("ATOMIC", BoundedIntArg(0,2) ,_atomic)
            .complete() < 0)
        return -1;

    if (!count_call && !byte_count_call) {
        if (norate)
            _simple = true;
    } else {
        if (norate)
            errh->warning("NO_RATE cannot be set when handlers are used. It will be ignored");
    }


    if (count_call) {
        IntArg ia;
        if (!ia.parse_saturating(cp_shift_spacevec(count_call), _count_trigger))
            return errh->error("COUNT_CALL type mismatch");
        else if (ia.status == IntArg::status_range)
            errh->error("COUNT_CALL overflow, max %s", String(_count_trigger).c_str());
        _count_trigger_h = new HandlerCall(count_call);
    } else
        _count_trigger = (counter_int_type)(-1);

    if (byte_count_call) {
        IntArg ia;
        if (!ia.parse_saturating(cp_shift_spacevec(byte_count_call), _byte_trigger))
            return errh->error("BYTE_COUNT_CALL type mismatch");
        else if (ia.status == IntArg::status_range)
            errh->error("BYTE_COUNT_CALL overflow, max %s", String(_count_trigger).c_str());
        _byte_trigger_h = new HandlerCall(byte_count_call);
    } else
        _byte_trigger = (counter_int_type)(-1);

    if ((count_call || byte_count_call) && name() == "CounterMP") {
        return errh->error("CounterMP cannot use handler calls");
    }

    return 0;
}

int
CounterBase::initialize(ErrorHandler *errh)
{
    if (_count_trigger_h && _count_trigger_h->initialize_write(this, errh) < 0)
        return -1;
    if (_byte_trigger_h && _byte_trigger_h->initialize_write(this, errh) < 0)
        return -1;
    reset();

    if (_atomic > 0 && can_atomic() == 0) {
        return errh->error("Sorry, %s does not support atomic",class_name());
    } else if (_atomic == 2 && can_atomic() < 2) {
        return errh->error("Sorry, %s does not support full-atomic mode",class_name());
    }
    return 0;
}

bool CounterBase::do_mt_safe_check(ErrorHandler* errh) {
    Bitvector bmp = get_passing_threads();
    int n = bmp.weight();
    if (n <= 1)
        return true;
    if (!_simple && _atomic == 0) {
        errh->error("Set ATOMIC to true if using handlers or rate");
        return false;
    }
    return true;
}

enum { H_COUNT, H_BYTE_COUNT, H_RESET,
    H_COUNT_CALL, H_BYTE_COUNT_CALL,
    H_RATE = 0x100, H_BIT_RATE, H_BYTE_RATE,};

String
CounterBase::read_handler(Element *e, void *thunk)
{
    CounterBase *c = (CounterBase *)e;
    if (c->cast("CounterMP") != 0 && (((intptr_t)thunk) & H_RATE)) {
        return "CounterMP does not support rate operations";
    }
    switch ((intptr_t)thunk) {
    case H_COUNT:
        return String(c->count());
    case H_BYTE_COUNT:
        return String(c->byte_count());
    case H_RATE:
        c->_rate.update(0);	// drop rate after idle period
        return c->_rate.unparse_rate();
    case H_BIT_RATE:
        c->_byte_rate.update(0); // drop rate after idle period
        // avoid integer overflow by adjusting scale factor instead of
        // multiplying
        if (c->_byte_rate.scale() >= 3)
            return cp_unparse_real2(c->_byte_rate.scaled_average() * c->_byte_rate.epoch_frequency(), c->_byte_rate.scale() - 3);
        else
            return cp_unparse_real2(c->_byte_rate.scaled_average() * c->_byte_rate.epoch_frequency() * 8, c->_byte_rate.scale());
    case H_BYTE_RATE:
        c->_byte_rate.update(0); // drop rate after idle period
        return c->_byte_rate.unparse_rate();
    case H_COUNT_CALL:
        if (c->_count_trigger_h)
            return String(c->_count_trigger);
        else
            return String();
    default:
        return "<error>";
    }
}

int
CounterBase::write_handler(const String &in_str, Element *e, void *thunk, ErrorHandler *errh)
{
    CounterBase *c = (CounterBase *)e;
    String str = cp_uncomment(in_str);
    switch ((intptr_t)thunk) {
    case H_COUNT_CALL:
        if (!IntArg().parse(cp_shift_spacevec(str), c->_count_trigger))
            return errh->error("'count_call' first word should be unsigned (count)");
        if (HandlerCall::reset_write(c->_count_trigger_h, str, c, errh) < 0)
            return -1;
        c->_count_triggered = false;
        return 0;
    case H_BYTE_COUNT_CALL:
        if (!IntArg().parse(cp_shift_spacevec(str), c->_byte_trigger))
            return errh->error("'byte_count_call' first word should be unsigned (count)");
        if (HandlerCall::reset_write(c->_byte_trigger_h, str, c, errh) < 0)
            return -1;
        c->_byte_triggered = false;
        return 0;
    case H_RESET:
        c->reset();
        return 0;
    default:
        return errh->error("<internal>");
    }
}

void
CounterBase::add_handlers()
{
    add_read_handler("count", CounterBase::read_handler, H_COUNT);
    add_read_handler("byte_count", CounterBase::read_handler, H_BYTE_COUNT);
    if (cast("Counter") != 0) {
        add_read_handler("rate", CounterBase::read_handler, H_RATE);
        add_read_handler("bit_rate", CounterBase::read_handler, H_BIT_RATE);
        add_read_handler("byte_rate", CounterBase::read_handler, H_BYTE_RATE);
        add_read_handler("count_call", CounterBase::read_handler, H_COUNT_CALL);
        add_write_handler("count_call", CounterBase::write_handler, H_COUNT_CALL);
        add_write_handler("byte_count_call", CounterBase::write_handler, H_BYTE_COUNT_CALL);
    }
    add_write_handler("reset", CounterBase::write_handler, H_RESET, Handler::f_button);
    add_write_handler("reset_counts", CounterBase::write_handler, H_RESET, Handler::f_button | Handler::f_uncommon);
}

int
CounterBase::llrpc(unsigned command, void *data)
{
    if (command == CLICK_LLRPC_GET_RATE) {
        uint32_t *val = reinterpret_cast<uint32_t *>(data);
        if (*val != 0)
            return -EINVAL;
        _rate.update(0);		// drop rate after idle period
        *val = _rate.rate();
        return 0;

    } else if (command == CLICK_LLRPC_GET_COUNT) {
        uint32_t *val = reinterpret_cast<uint32_t *>(data);
        if (*val != 0 && *val != 1)
            return -EINVAL;
        *val = (*val == 0 ? count() : byte_count());
        return 0;

    } else if (command == CLICK_LLRPC_GET_COUNTS) {
        click_llrpc_counts_st *user_cs = (click_llrpc_counts_st *)data;
        click_llrpc_counts_st cs;
        if (CLICK_LLRPC_GET_DATA(&cs, data, sizeof(cs.n) + sizeof(cs.keys)) < 0
                || cs.n >= CLICK_LLRPC_COUNTS_SIZE)
            return -EINVAL;
        for (unsigned i = 0; i < cs.n; i++) {
            if (cs.keys[i] == 0)
                cs.values[i] = count();
            else if (cs.keys[i] == 1)
                cs.values[i] = byte_count();
            else
                return -EINVAL;
        }
        return CLICK_LLRPC_PUT_DATA(&user_cs->values, &cs.values, sizeof(cs.values));

    } else
        return Element::llrpc(command, data);
}

Counter::Counter()
{
}

Counter::~Counter()
{
}

Packet*
Counter::simple_action(Packet *p)
{
    _count++;
    _byte_count += p->length();

    if (likely(_simple))
        return p;
    _rate.update(1);
    _byte_rate.update(p->length());

    check_handlers(_count, _byte_count);
    return p;
}


#if HAVE_BATCH
PacketBatch*
Counter::simple_action_batch(PacketBatch *batch)
{
    if (unlikely(_batch_precise)) {
        FOR_EACH_PACKET(batch,p)
                                Counter::simple_action(p);
        return batch;
    }

    counter_int_type bc = 0;
    FOR_EACH_PACKET(batch,p) {
        bc += p->length();
    }

    _count += batch->count();
    _byte_count += bc;

    if (likely(_simple))
        return batch;
    _rate.update(batch->count());
    _byte_rate.update(bc);

    check_handlers(_count, _byte_count);
    return batch;
}
#endif

void
Counter::reset()
{
    _count = 0;
    _byte_count = 0;
    CounterBase::reset();
}


template class CounterMPBase<nolock>;

CLICK_ENDDECLS

EXPORT_ELEMENT(Counter)
EXPORT_ELEMENT(CounterMP)
ELEMENT_MT_SAFE(CounterMP)
