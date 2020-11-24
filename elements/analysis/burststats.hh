// -*- c-basic-offset: 4 -*-
#ifndef CLICK_BURSTSTATS_HH
#define CLICK_BURSTSTATS_HH
#include <click/batchelement.hh>
#include <click/multithread.hh>
#include <click/vector.hh>
#include <click/statvector.hh>
CLICK_DECLS

/*
=c

BurstStats

=s counters

keep statistics about bursts, defined as the number of packets from the same flow following each others

=d

handlers

* average : Average burst size
* median : Median burst size
* dump : Print the number of batches for each size seen

 */

class BurstStats : public SimpleElement<BurstStats>, public StatVector<int> { public:

    BurstStats() CLICK_COLD;
    ~BurstStats() CLICK_COLD;

    const char *class_name() const override	{ return "BurstStats"; }
    const char *port_count() const override	{ return PORTS_1_1; }
    void * cast(const char *name);

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;
    void cleanup(CleanupStage) CLICK_COLD;

    Packet *simple_action(Packet *) override;

    void add_handlers();

    struct BurstStatsState {
        BurstStatsState() : burstlen(0) {

        }
        int burstlen;
        int last_anno;
    };
    per_thread<BurstStatsState> s;
};

CLICK_ENDDECLS
#endif
