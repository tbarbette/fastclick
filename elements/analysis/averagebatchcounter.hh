// -*- c-basic-offset: 4 -*-
#ifndef CLICK_AVERAGEBATCHCOUNTER_HH
#define CLICK_AVERAGEBATCHCOUNTER_HH
#include <click/batchelement.hh>
#include <click/multithread.hh>
#include <click/timer.hh>
CLICK_DECLS

/*
=c

AverageBatchCounter([INTERVAL, LENGTH_STATS])

=s counters

keep average statistics about batching since last reset and last tick

=d

Expects Ethernet frames as input. Computes statistics related to the batches
being created either since the last tick or since the initialization of the
element.
If LENGTH_STATS is set to true, this element should be combined with an
upstream AggregateLength element, which provides frames' length.

Keyword arguments are:

=over 2

=item INTERVAL

Integer. Time interval to reschedule the computation of the statistics.
Defaults to 1000 ms.

=item LENGTH_STATS

Boolean. If set, provides additional statistics regarding frames' length.
Defaults to false.

=h average read-only

Returns the average batch size (packets/batch) since the last tick.

=h average_total read-only

Returns the total average batch size (packets/batch).

=h count_packets read-only

Returns the number of packets seen since the last tick.

=h count_packets_total read-only

Returns the total number of packets seen.

=h count_batches read-only

Returns the number of batches created since the last tick.

=h count_batches_total read-only

Returns the total number of batches.

=h average_frame_len read-only

Returns the average frame length in bytes since the last tick.

=h average_frame_len_total read-only

Returns the total average frame length in bytes.

 */

class AverageBatchCounter : public BatchElement { public:

    AverageBatchCounter() CLICK_COLD;
    ~AverageBatchCounter() CLICK_COLD;

    const char *class_name() const override	{ return "AverageBatchCounter"; }
    const char *port_count() const override	{ return PORTS_1_1; }

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
        uint64_t count_bytes;
        float avg_frame_len;

        BatchStats() : count_batches(0), count_packets(0), count_bytes(0), avg_frame_len(0.0f) {

        }
    };
    per_thread<BatchStats> _stats;
    static String read_handler(Element *e, void *thunk);

    uint32_t compute_agg_frame_len(PacketBatch *batch);

    /*
     * Unprotected RCU is fine as the stats will be written once every _interval, no way a handler stays
     * alive for 2*interval of time.
     */
    unprotected_rcu_singlewriter<BatchStats,2> _stats_last_tick;
    unprotected_rcu_singlewriter<BatchStats,2> _stats_total;

    int _interval;
    bool _frame_len_stats;
    Timer _timer;

    enum {
        H_AVERAGE, H_AVERAGE_TOTAL,
        H_COUNT_PACKETS, H_COUNT_PACKETS_TOTAL,
        H_COUNT_BATCHES, H_COUNT_BATCHES_TOTAL,
        H_AVG_FRAME_LEN, H_AVG_FRAME_LEN_TOTAL
    };

};

CLICK_ENDDECLS
#endif
