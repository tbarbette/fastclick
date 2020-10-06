// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_AGGREGATELEN_HH
#define CLICK_AGGREGATELEN_HH
#include <click/batchelement.hh>
CLICK_DECLS

/*
=c

AggregateLength(I<KEYWORDS>)

=s aggregates

sets aggregate annotation based on packet length

=d

AggregateLength sets the aggregate annotation on every passing packet to the
packet's length, plus any length stored in the extra length annotation.

Keyword arguments are:

=over 8

=item IP

Boolean. If true, then only include length starting at the IP header. Default
is false.

=back

=n

If IP is true, then packets without a network header are pushed onto output 1,
or dropped if there is no output 1.

=a

AggregateIP, AggregateCounter

*/

class AggregateLength : public BatchElement { public:

    AggregateLength() CLICK_COLD;
    ~AggregateLength() CLICK_COLD;

    const char *class_name() const override	{ return "AggregateLength"; }
    const char *port_count() const override	{ return PORTS_1_1; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push(int, Packet *);
    Packet *pull(int);
#if HAVE_BATCH
    void push_batch(int, PacketBatch*) override;
    PacketBatch *pull_batch(int,unsigned) override;
#endif

  private:

    bool _ip;

    Packet *handle_packet(Packet *);
    Packet *bad_packet(Packet *);

};

CLICK_ENDDECLS
#endif
