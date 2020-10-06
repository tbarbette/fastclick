#ifndef CLICK_DISCARDNOFREEBUFFER_HH
#define CLICK_DISCARDNOFREEBUFFER_HH
#include <click/batchelement.hh>
#include <click/task.hh>
CLICK_DECLS

/*
 * =c
 * DiscardNoFreeBuffer
 * =s basicsources
 * drops all packets, but does not free their buffers. The packet
 * descriptor (Packet object) is recycled though.
 * =d
 * Discards all packets received on its single input, but does not free their
 * buffer. Only useful for benchmarking or in combination to DPDK and Replay
 * (with QUICK_CLONE) for packet generator.
 */

class DiscardNoFreeBuffer : public BatchElement { public:

  DiscardNoFreeBuffer() CLICK_COLD;

  const char *class_name() const override		{ return "DiscardNoFreeBuffer"; }
  const char *port_count() const override		{ return PORTS_1_0; }

  int initialize(ErrorHandler *) CLICK_COLD;
  void add_handlers() CLICK_COLD;

  void push(int, Packet *);
#if HAVE_BATCH
  void push_batch(int, PacketBatch *);
#endif
  bool run_task(Task *);

 private:

  Task _task;

};

CLICK_ENDDECLS
#endif
