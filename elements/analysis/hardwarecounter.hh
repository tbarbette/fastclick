// -*- c-basic-offset: 4 -*-
#ifndef CLICK_HARDWARECOUNTER_HH
#define CLICK_HARDWARECOUNTER_HH
#include <click/batchelement.hh>
#include <click/multithread.hh>
#include <click/vector.hh>
CLICK_DECLS

/*
=c

HardwareCounter

=s analysis

keep statistics about batching

 */

class HardwareCounter : public BatchElement { public:

    HardwareCounter() CLICK_COLD;
    ~HardwareCounter() CLICK_COLD;

    const char *class_name() const	{ return "HardwareCounter"; }
    const char *port_count() const	{ return PORTS_0_0; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;
    void cleanup(CleanupStage) CLICK_COLD;

    void add_handlers();

    struct event {
        String name;
        int idx;
        String desc;
    };
private:


    static String read_handler(Element *e, void *thunk);
    static int write_handler(const String &data, Element *e,
                        void *thunk, ErrorHandler *errh);
    Vector<event> _events;
    Vector<long long> _accum_values;;

    enum {h_dump_read = -1, h_dump_accum = -2,  h_list = -3, h_accum = -4, h_reset_accum = -5};
};

CLICK_ENDDECLS
#endif
