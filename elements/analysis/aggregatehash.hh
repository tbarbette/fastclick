// -*- c-basic-offset: 4 -*-
#ifndef CLICK_AGGREGATEhash_HH
#define CLICK_AGGREGATEhash_HH
#include <click/batchelement.hh>
CLICK_DECLS

/*
=c

AggregateHash([BITS, I<KEYWORDS>])

=s aggregates

sets aggregate annotation as a hash of the 5-tuple

=d

AggregateHash sets the aggregate annotation on every passing packet to a
a hash of the 5-tuple.

=a

AggregateLength, AggregateIPFlows, AggregateCounter, AggregateIP

*/

class AggregateHash : public SimpleElement<AggregateHash> { public:

    AggregateHash() CLICK_COLD;
    ~AggregateHash() CLICK_COLD;

    const char *class_name() const override	{ return "AggregateHash"; }
    const char *port_count() const override	{ return PORTS_1_1; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    Packet *simple_action(Packet *);

  private:

    int _bits;

};

CLICK_ENDDECLS
#endif
