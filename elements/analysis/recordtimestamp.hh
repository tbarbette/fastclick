#ifndef CLICK_RECORDTIMESTAMP_HH
#define CLICK_RECORDTIMESTAMP_HH

#include <click/vector.hh>
#include <click/batchelement.hh>
#include <click/timestamp.hh>

CLICK_DECLS

/*
=c

RecordTimestamp([I<keywords> N])

=s timestamps

record current timestamp in a vector each time a packet is pushed through this
element.

=item N

Size of the vector, defaults to 65536.

=item OFFSET

If offset is set, the slot in the vector will be read from packet, assuming that
the packet was previously being marked using a NumberPacket element.
If not set or < 0, the vector will be filled in order.

=item DYNAMIC

If true, allows to grow the vector on runtime. This is disabled by default because it is not multi thread safe a,d creates a spike in latency that is due to the long time taken to resize. If
the number of packets reaches a non dynamic TimestampDiff, it will crash.

=a

NumberPacket, TimestampDiff
*/
class RecordTimestamp : public BatchElement {
public:
    RecordTimestamp() CLICK_COLD;
    ~RecordTimestamp() CLICK_COLD;

    const char *class_name() const { return "RecordTimestamp"; }
    const char *port_count() const { return PORTS_1_1; }
    const char *processing() const { return PUSH; }
    const char *flow_code() const { return "x/x"; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    inline void smaction(Packet *);
    void push(int, Packet *);
#if HAVE_BATCH
    void push_batch(int, PacketBatch *);
#endif

    inline Timestamp get(uint64_t i);

private:
    int _offset;
    bool _dynamic;
    Vector<Timestamp> _timestamps;
};

inline Timestamp RecordTimestamp::get(uint64_t i) {
    if (i >= _timestamps.size())
        return Timestamp::uninitialized_t();
    Timestamp t = _timestamps[i];
    if (t == Timestamp::uninitialized_t())
        return Timestamp::uninitialized_t();
    _timestamps[i] = Timestamp::uninitialized_t();
    return t;
}

CLICK_ENDDECLS

#endif
