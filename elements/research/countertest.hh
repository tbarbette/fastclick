// -*- c-basic-offset: 4 -*-
#ifndef CLICK_COUNTERTEST_HH
#define CLICK_COUNTERTEST_HH
#include <click/batchelement.hh>
#include "../standard/counter.hh"
CLICK_DECLS

/*
=c

CounterTest()

=s test

Call read_atomic on the counter in a certain proportion regarding the batch
it passes.

*/

class CounterTest : public BatchElement {
    public:
        CounterTest() CLICK_COLD;

        const char *class_name() const { return "CounterTest"; }
        const char *port_count() const { return "0-1/1"; }
        const char *processing() const { return PUSH; }

        int configure(Vector<String>&, ErrorHandler*) override;
        bool run_task(Task *) override;
        void push(int, Packet* p) override;
    #if HAVE_BATCH
        void push_batch(int, PacketBatch* batch) override;
    #endif
        void add_handlers() override;

    private:
        CounterBase* _counter;
        int _rate;
        unsigned long _read;
        unsigned long _write;
        bool _atomic;
        bool _standalone;
        Task _task;
        int _pass;
        per_thread<int> _cur_pass;
#if defined(__GNUC__) && !defined(__clang__)
        void(*_add_fnt)(CounterBase*,CounterBase::stats);
        CounterBase::stats(*_read_fnt)(CounterBase*);
#endif
};

CLICK_ENDDECLS
#endif
