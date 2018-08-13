// -*- c-basic-offset: 4 -*-
#ifndef CLICK_AVERAGEBATCHCOUNTER_HH
#define CLICK_AVERAGEBATCHCOUNTER_HH
#include <click/batchelement.hh>
#include <click/multithread.hh>
#include <click/timer.hh>
CLICK_DECLS

/*
=c

AverageBatchCounter

=s counters

keep average statistics about batching since last reset and last tick

 */

class AverageBatchCounter : public BatchElement { public:

    AverageBatchCounter() CLICK_COLD;
    ~AverageBatchCounter() CLICK_COLD;

    const char *class_name() const	{ return "AverageBatchCounter"; }
    const char *port_count() const	{ return PORTS_1_1; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;
    void cleanup(CleanupStage) CLICK_COLD;

    void run_timer(Timer* t);

    PacketBatch *simple_action_batch(PacketBatch *) override;

    void add_handlers();
private:

    struct BatchStats {
        uint64_t count_batches;
        uint64_t count_packets;

        BatchStats() : count_batches(0), count_packets(0) {

        }
    };
    per_thread<BatchStats> _stats;
    static String read_handler(Element *e, void *thunk);

    /*
     * Unprotected RCU is fine as the stats will be written once every _interval, no way a handler stays
     * alive for 2*interval of time.
     */
    unprotected_rcu_singlewriter<BatchStats,2> _stats_last_tick;
    unprotected_rcu_singlewriter<BatchStats,2> _stats_total;

    int _interval;
    Timer _timer;

    enum {H_AVERAGE, H_AVERAGE_TOTAL, H_COUNT_PACKETS, H_COUNT_PACKETS_TOTAL, H_COUNT_BATCHES, H_COUNT_BATCHES_TOTAL };

};

CLICK_ENDDECLS
#endif
