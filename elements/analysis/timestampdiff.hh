#ifndef CLICK_TIMESTAMPDIFF_HH
#define CLICK_TIMESTAMPDIFF_HH

#include <click/vector.hh>
#include <click/batchelement.hh>

CLICK_DECLS

class RecordTimestamp;

/*
=c

TimestampDiff()

Compute the RTT of packets marked with RecordTimestamp

=s timestamps

=d

Compute the difference between the recorded timestamp of a packet using
RecordTimestamp and a fresh timestamp. Computations are performed only for
numbered packets, either by a NumberPacket element or an external entity.

Arguments:

=item RECORDER

Instance of RecordTimestamp that provides the initial timestamp.

=item OFFSET

Integer. Offset in the packet where the timestamp resides.

=item N

Size of the reservoir. Defaults to 0.

=item MAXDELAY

Integer. Maximum delay in milliseconds. If a packet exhibits such a delay (or greater),
the user is notified. Defaults to 1000 ms (1 sec).

=a

RecordTimestamp, NumberPacket

*/
class TimestampDiff : public BatchElement {
public:
    TimestampDiff() CLICK_COLD;
    ~TimestampDiff() CLICK_COLD;

    const char *class_name() const { return "TimestampDiff"; }
    const char *port_count() const { return PORTS_1_1X2; }
    const char *processing() const { return PUSH; }
    const char *flow_code() const { return "x/x"; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;
    void add_handlers() CLICK_COLD;
    static int handler(int operation, String &data, Element *element,
            const Handler *handler, ErrorHandler *errh) CLICK_COLD;

    void push(int, Packet *);
#if HAVE_BATCH
    void push_batch(int, PacketBatch *);
#endif

private:
    Vector<unsigned> _delays;
    int _offset;
    uint32_t _limit;
    bool _net_order;
    int _max_delay_ms;
    RecordTimestamp *_rt;
    //Current index in the delays
    atomic_uint32_t _nd;
    bool _verbose;

    inline int smaction(Packet *p);

    RecordTimestamp *get_recordtimestamp_instance();

    void min_mean_max(
        unsigned &min,
        double   &mean,
        unsigned &max,
        uint32_t begin = 0
    );
    double standard_deviation(const double mean, uint32_t begin = 0);
    double percentile(const double percent, uint32_t begin = 0);
    unsigned last_value_seen();
};

CLICK_ENDDECLS

#endif
