// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_STRIP_HH
#define CLICK_STRIP_HH
#include <click/batchelement.hh>
CLICK_DECLS

/*
 * =c
 * Strip(LENGTH)
 * =s basicmod
 * strips bytes from front of packets
 * =d
 * Deletes the first LENGTH bytes from each packet.
 * =e
 * Use this to get rid of the Ethernet header:
 *
 *   Strip(14)
 * =a StripToNetworkHeader, StripIPHeader, EtherEncap, IPEncap, Truncate
 */

class Strip : public BatchElement { public:

    Strip() CLICK_COLD;

    const char *class_name() const override		{ return "Strip"; }
    const char *port_count() const override		{ return PORTS_1_1; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch *);
#endif
  Packet *simple_action(Packet *);

  private:

    unsigned _nbytes;

};

CLICK_ENDDECLS
#endif
