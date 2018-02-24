// -*- c-basic-offset: 4 -*-
#ifndef CLICK_STOREDATA_HH
#define CLICK_STOREDATA_HH
#include <click/batchelement.hh>
CLICK_DECLS

/* =c
 * StoreData(OFFSET, DATA)
 * =s basicmod
 * changes packet data
 * =d
 *
 * Changes packet data starting at OFFSET to DATA.
 *
 * =a AlignmentInfo, click-align(1) */

class StoreData : public BatchElement { public:

    StoreData() CLICK_COLD;

    const char *class_name() const		{ return "StoreData"; }
    const char *port_count() const		{ return PORTS_1_1; }
    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    Packet *simple_action(Packet *);
#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch *);
#endif
  private:

    unsigned _offset;
    String _data;
    bool _grow;

};

CLICK_ENDDECLS
#endif
