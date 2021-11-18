// -*- c-basic-offset: 4 -*-
#ifndef CLICK_PAD_HH
#define CLICK_PAD_HH
#include <click/batchelement.hh>
CLICK_DECLS

/*
=c

Pad([LENGTH, I<keyword> ZERO])

=s basicmod

extend packet length

=d

Extend packets to at least LENGTH bytes.

If LENGTH is omitted, then input packets are extended to the length
indicated by their extra length annotations. Output packets always have
extra length annotation zero.

Keyword arguments are:

=over 8

=item ZERO

Boolean. If true, then set added packet data to zero; if false, then
additional packet data is left uninitialized (which might be a security
problem). Default is true.

=item RANDOM

Boolean. If true, the added data is randomized. Exclusive with ZERO. Default is
false.

=item MAXLENGTH

Int. If >0, it specifies the maximum length that a packet can have after padding.
If the final length would be higher than this, the packet will be truncated to MAXLENGTH

=back

=a Truncate
*/

class Pad : public SimpleElement<Pad> { public:

    Pad() CLICK_COLD;

    const char *class_name() const override		{ return "Pad"; }
    const char *port_count() const override		{ return PORTS_1_1; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    bool can_live_reconfigure() const		{ return true; }

    Packet *simple_action(Packet *);

  private:

    unsigned _nbytes;
    unsigned _maxlength;
    bool _zero;
    bool _verbose;
    bool _random;
};

CLICK_ENDDECLS
#endif
