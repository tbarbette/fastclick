#ifndef CLICK_RECORDTIMESTAMP_HH
#define CLICK_RECORDTIMESTAMP_HH

#include <cassert>
#include <vector>

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

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push(int, Packet *);
#if HAVE_BATCH
    void push_batch(int, PacketBatch *);
#endif

    Timestamp get(uint64_t i);

private:
    uint64_t _count;
    std::vector<Timestamp> _timestamps;
};

inline Timestamp RecordTimestamp::get(uint64_t i) {
    assert(i < _timestamps.size());
    Timestamp t = _timestamps[i];
    assert(t != Timestamp::uninitialized_t());
    _timestamps[i] = Timestamp::uninitialized_t();
    return t;
}

extern RecordTimestamp *recordtimestamp_singleton_instance;

inline RecordTimestamp *get_recordtimestamp_instance() {
    return recordtimestamp_singleton_instance;
}


CLICK_ENDDECLS

#endif
