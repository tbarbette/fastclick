#ifndef CLICK_UNSTRIP_HH
#define CLICK_UNSTRIP_HH
#include <click/batchelement.hh>
CLICK_DECLS

/*
 * =c
 * Unstrip(LENGTH)
 * =s basicmod
 * unstrips bytes from front of packets
 * =d
 * Put LENGTH bytes at the front of the packet. These LENGTH bytes may be bytes
 * previously removed by Strip.
 * =e
 * Use this to get rid of the Ethernet header and put it back on:
 *
 *   Strip(14) -> ... -> Unstrip(14)
 * =a EtherEncap, IPEncap
 */

class Unstrip : public BatchElement {

  unsigned _nbytes;

 public:

  Unstrip(unsigned nbytes = 0);

  const char *class_name() const	{ return "Unstrip"; }
  const char *port_count() const	{ return PORTS_1_1; }

  int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;

#if HAVE_BATCH
  PacketBatch *simple_action_batch(PacketBatch *);
#endif
  Packet *simple_action(Packet *);

};

CLICK_ENDDECLS
#endif
