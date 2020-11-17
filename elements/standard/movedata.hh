// -*- c-basic-offset: 4 -*-
#ifndef CLICK_MOVEDATA_HH
#define CLICK_MOVEDATA_HH
#include <click/batchelement.hh>
CLICK_DECLS

/* =c
 * MoveData(DST_OFFSET, SRC_OFFSET, LENGTH, [GROW])
 * =s basicmod
 * changes packet data
 * =d
 * Will copy LENGTH bytes from SRC to DST. If grow and there is not enough room for DST+LENGTH, packet will be expanded. If not grow, copy will not be done.
 *
  */
class MoveData : public BatchElement { public:

    MoveData() CLICK_COLD;

    const char *class_name() const override		{ return "MoveData"; }
    const char *port_count() const override		{ return PORTS_1_1; }
    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;

    Packet *simple_action(Packet *);
#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch *);
#endif
  private:

    int _src_offset;
    int _dst_offset;
    unsigned _length;
    bool _grow;

};

CLICK_ENDDECLS
#endif
