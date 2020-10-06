#ifndef CLICK_UnstripTransportHeader_HH
#define CLICK_UnstripTransportHeader_HH
#include <click/batchelement.hh>
CLICK_DECLS

/*
 * =c
 * UnstripTransportHeader()
 * =s ip
 * restores outermost transport header
 * =d
 *
 * Put outermost transport header back onto a stripped packet, based
 * on the transport header annotation from, e.g. MarkIPHeader or CheckIPHeader.
 * If transport header already on forwards packet unmodified.
 *
 * Beware that this element will not "undo" StripTransportHeader.
 * StripTransportHeader will jump from anywhere to the first byte of payload
 * after the transport header. This element will return to the byte before the
 * transport header.
 *
 * =a CheckIPHeader, MarkIPHeader, StripIPHeader, StripTransportHeader */

class UnstripTransportHeader : public BatchElement {

public:
  UnstripTransportHeader() CLICK_COLD;
  ~UnstripTransportHeader() CLICK_COLD;

  const char *class_name() const override		{ return "UnstripTransportHeader"; }
  const char *port_count() const override		{ return PORTS_1_1; }

  Packet      *simple_action      (Packet      *p);
#if HAVE_BATCH
  PacketBatch *simple_action_batch(PacketBatch *batch);
#endif
};

CLICK_ENDDECLS
#endif
