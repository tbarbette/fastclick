// -*- c-basic-offset: 4 -*-
#ifndef CLICK_BATCHSTATS_HH
#define CLICK_BATCHSTATS_HH
#include <click/batchelement.hh>
#include <click/multithread.hh>
#include <click/vector.hh>
#include <click/statvector.hh>
CLICK_DECLS

/*
=c

BatchStats

=s counters

keep statistics about batching

=d

Remembers the size of every batch passing by, and displays various statistics about the batch sizes

handlers

* average : Average batch size
* median : Median batch size
* dump : Print the number of batches for each size seen

 */

class BatchStats : public BatchElement, StatVector<int> { public:

    BatchStats() CLICK_COLD;
    ~BatchStats() CLICK_COLD;

    const char *class_name() const override	{ return "BatchStats"; }
    const char *port_count() const override	{ return PORTS_1_1; }
    void * cast(const char *name);

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;
    void cleanup(CleanupStage) CLICK_COLD;

    Packet *simple_action(Packet *) override;
#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch *) override;
#endif

    void add_handlers();

};

CLICK_ENDDECLS
#endif
