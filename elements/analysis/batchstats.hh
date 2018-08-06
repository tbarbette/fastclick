// -*- c-basic-offset: 4 -*-
#ifndef CLICK_BATCHSTATS_HH
#define CLICK_BATCHSTATS_HH
#include <click/batchelement.hh>
#include <click/multithread.hh>
#include <click/vector.hh>
CLICK_DECLS

/*
=c

BatchStats

=s counters

keep statistics about batching

 */

class BatchStats : public BatchElement { public:

    BatchStats() CLICK_COLD;
    ~BatchStats() CLICK_COLD;

    const char *class_name() const	{ return "BatchStats"; }
    const char *port_count() const	{ return PORTS_1_1; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;
    void cleanup(CleanupStage) CLICK_COLD;

    Packet *simple_action(Packet *) override;
#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch *) override;
#endif

    void add_handlers();
private:

    per_thread_omem<Vector<int>> stats;
    enum{H_MEDIAN,H_AVERAGE,H_DUMP};
    static String read_handler(Element *e, void *thunk);
};

CLICK_ENDDECLS
#endif
