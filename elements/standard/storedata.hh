// -*- c-basic-offset: 4 -*-
#ifndef CLICK_STOREDATA_HH
#define CLICK_STOREDATA_HH
#include <click/batchelement.hh>
CLICK_DECLS

/* =c
 * StoreData(OFFSET, DATA, [MASK], I[<KEYWORDS>])
 * =s basicmod
 * changes packet data
 * =d
 *
 * Changes packet data starting at OFFSET to DATA.
 *
 * Optionally MASK can be specified, so only these bits
 * of packet data are changed which corresponding bits
 * of MASK are 1.
 *
 * DATA and MASK can be specified is hexadecimal using
 * the following syntax:
 *
 *   StoreData(1, \<FF>, MASK \<02>);
 *
 * Keyword arguments are:
 *
 * =over 8
 *
 * =item GROW
 *
 * When set to true and DATA exceeds past packet length,
 * packet length will be extended. Otherwise, excessive data
 * will not be stored. Default is false.
 *
 * =a AlignmentInfo, click-align(1) */

class StoreData : public BatchElement { public:

    StoreData() CLICK_COLD;

    const char *class_name() const override		{ return "StoreData"; }
    const char *port_count() const override		{ return PORTS_1_1; }
    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;

    Packet *simple_action(Packet *);
#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch *);
#endif
  private:

    unsigned _offset;
    String _data;
    String _mask;
    bool _grow;

};

CLICK_ENDDECLS
#endif
