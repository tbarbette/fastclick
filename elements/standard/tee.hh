#ifndef CLICK_TEE_HH
#define CLICK_TEE_HH
#include <click/batchelement.hh>
CLICK_DECLS

/*
 * =c
 * Tee([N])
 *
 * PullTee([N])
 * =s basictransfer
 * duplicates packets
 * =d
 * Tee sends a copy of each incoming packet out each output.
 *
 * PullTee's input and its first output are pull; its other outputs are push.
 * Each time the pull output pulls a packet, it
 * sends a copy out the push outputs.
 *
 * Tee and PullTee have however many outputs are used in the configuration,
 * but you can say how many outputs you expect with the optional argument
 * N.
 */

class Tee : public BatchElement {

 public:

  Tee() CLICK_COLD;

  const char *class_name() const override		{ return "Tee"; }
  const char *port_count() const override		{ return "1/1-"; }
  const char *processing() const override		{ return PUSH; }

  int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

  void push(int, Packet *) override;
  #if HAVE_BATCH
  void push_batch(int, PacketBatch *) override;
  #endif

};

class PullTee : public BatchElement {

 public:

  PullTee() CLICK_COLD;

  const char *class_name() const override		{ return "PullTee"; }
  const char *port_count() const override		{ return "1/1-"; }
  const char *processing() const override		{ return "l/lh"; }

  int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

  Packet *pull(int) override;
  #if HAVE_BATCH
  PacketBatch *pull_batch(int, unsigned) override;
  #endif

};

CLICK_ENDDECLS
#endif
