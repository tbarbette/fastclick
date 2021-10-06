// -*- c-basic-offset: 4 -*-
#ifndef CLICK_STOREANNO_HH
#define CLICK_STOREANNO_HH
#include <click/batchelement.hh>
CLICK_DECLS

/* =c
 * StoreAnno(OFFSET, ANNO, I[<KEYWORDS>])
 * =s basicmod
 * changes packet data
 * =d
 *
 * Changes packet data starting at OFFSET to the value of an annotation ANNO.
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

class StoreAnno : public BatchElement { public:

    StoreAnno() CLICK_COLD;

    const char *class_name() const		{ return "StoreAnno"; }
    const char *port_count() const		{ return PORTS_1_1; }
    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;

    Packet *simple_action(Packet *);
#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch *);
#endif
  private:

    unsigned _offset;
    int _anno;
    bool _grow;

};

CLICK_ENDDECLS
#endif
