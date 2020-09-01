// -*- c-basic-offset: 4 -*-
#ifndef CLICK_AGGSTATS_HH
#define CLICK_AGGSTATS_HH
#include <click/batchelement.hh>
#include <click/multithread.hh>
#include <click/vector.hh>
#include <click/statvector.hh>
CLICK_DECLS

/*
=c

AggregateStats

=s counters

keep statistics about frequency of aggregates passing by

handlers

* average : Average batch size
* median : Median batch size
* dump : Print the number of batches for each size seen

 */

class AggregateStats : public BatchElement, StatVector<int> { public:

    AggregateStats() CLICK_COLD;
    ~AggregateStats() CLICK_COLD;

    const char *class_name() const	{ return "AggregateStats"; }
    const char *port_count() const	{ return PORTS_1_1; }
    void * cast(const char *name);

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;
    void cleanup(CleanupStage) CLICK_COLD;

    Packet *simple_action(Packet *) override;
#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch *) override;
#endif

    void add_handlers();
private:
    unsigned _max;
};

CLICK_ENDDECLS
#endif
