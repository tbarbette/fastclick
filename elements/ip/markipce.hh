#ifndef CLICK_MARKIPCE_HH
#define CLICK_MARKIPCE_HH
#include <click/batchelement.hh>
#include <click/atomic.hh>
CLICK_DECLS

/*
=c

MarkIPCE([FORCE])

=s ip

sets IP packets' ECN field to Congestion Experienced

=d

Expects IP packets as input.  Sets each incoming packet's ECN field to
Congestion Experienced (value 3), incrementally recalculates the IP checksum,
and passes the packet to output 0.  Packets whose ECN field is zero (not
ECN-capable) are dropped unless the optional FORCE argument is true. */

class MarkIPCE : public SimpleElement<MarkIPCE> {
    public:
        MarkIPCE() CLICK_COLD;
        ~MarkIPCE() CLICK_COLD;

        const char *class_name() const override { return "MarkIPCE"; }
        const char *port_count() const override { return PORTS_1_1; }

        int configure(Vector<String> &conf, ErrorHandler *errh) CLICK_COLD;
        void add_handlers() CLICK_COLD;

        Packet *simple_action(Packet *);

    private:
        bool _force;
        atomic_uint64_t _drops;

        static String read_handler(Element *e, void *thunk) CLICK_COLD;
};

CLICK_ENDDECLS
#endif
