#ifndef CLICK_RECORDTIMESTAMP_HH
#define CLICK_RECORDTIMESTAMP_HH

#include <cassert>
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

Size of the vector, defaults to 65536

=item OFFSET
If offset is setted, the slot in the vector will be read from packet, assumed
to previously been marked with NumberPacket. If unset or < 0, the vector
will be filled in order.

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

    Timestamp get(uint64_t i);

private:
    int _offset;
    Vector<Timestamp> _timestamps;
};

inline Timestamp RecordTimestamp::get(uint64_t i) {
    assert(i < _timestamps.size());
    Timestamp t = _timestamps[i];
    assert(t != Timestamp::uninitialized_t());
    _timestamps[i] = Timestamp::uninitialized_t();
    return t;
}

CLICK_ENDDECLS

#endif
