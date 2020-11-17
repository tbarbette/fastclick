#ifndef CLICK_UNSTRIPIPHEADER_HH
#define CLICK_UNSTRIPIPHEADER_HH
#include <click/batchelement.hh>
CLICK_DECLS

/*
 * =c
 * UnstripIPHeader()
 * =s ip
 * restores outermost IP header
 * =d
 *
 * Put outermost IP header back onto a stripped packet, based on the IP Header
 * annotation from MarkIPHeader or CheckIPHeader. If IP header already on,
 * forwards packet unmodified.
 *
 * =a CheckIPHeader, MarkIPHeader, StripIPHeader */

class UnstripIPHeader : public BatchElement {

public:
  UnstripIPHeader() CLICK_COLD;
  ~UnstripIPHeader() CLICK_COLD;

  const char *class_name() const override		{ return "UnstripIPHeader"; }
  const char *port_count() const override		{ return PORTS_1_1; }

  Packet      *simple_action      (Packet      *p);
#if HAVE_BATCH
  PacketBatch *simple_action_batch(PacketBatch *batch);
#endif
};

CLICK_ENDDECLS
#endif
