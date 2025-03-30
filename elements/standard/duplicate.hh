#ifndef CLICK_Duplicate_HH
#define CLICK_Duplicate_HH
#include <click/batchelement.hh>
CLICK_DECLS

/*
 * =c
 * Duplicate([N])
 *
 * =s basictransfer
 * fully duplicates packets
 * =d
 * Duplicate sends a copy of each incoming packet out each output.
 * As opposed to Tee, this element always makes a full copy of the packet
 *
 *
 * Duplicate has however many outputs are used in the configuration,
 * but you can say how many outputs you expect with the optional argument
 * N.
 * 
 * @a see also Tee
 */

class Duplicate : public BatchElement {

 public:

  Duplicate() CLICK_COLD;

  const char *class_name() const override		{ return "Duplicate"; }
  const char *port_count() const override		{ return "1/1-"; }
  const char *processing() const override		{ return PUSH; }

  int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

  void push(int, Packet *) override;
  #if HAVE_BATCH
  void push_batch(int, PacketBatch *) override;
  #endif

  bool _data_only;

};

CLICK_ENDDECLS
#endif
