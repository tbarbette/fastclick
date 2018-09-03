#ifndef CLICK_RECORDTIMESTAMP_HH
#define CLICK_RECORDTIMESTAMP_HH

#include <click/vector.hh>
#include <click/batchelement.hh>
#include <click/timestamp.hh>
#include "numberpacket.hh"

CLICK_DECLS

class NumberPacket;

/*
=c

RecordTimestamp([I<keywords> N])

Record timestamp in memory

=s timestamps

=d

record current timestamp in a vector each time a packet is pushed through this
element.

=item COUNTER

Relies on a specific NumberPacket element to fetch the packet counter.
Defaults to no COUNTER element.

=item N

Size of the vector, defaults to 65536.

=item OFFSET

If offset is set, the slot in the vector will be read from packet, assuming that
the packet was previously being marked using a NumberPacket element.
If not set or < 0, the vector will be filled in order.

=item DYNAMIC

If true, allows to grow the vector on runtime. This is disabled by default because it is not multi thread safe a,d creates a spike in latency that is due to the long time taken to resize. If
the number of packets reaches a non dynamic TimestampDiff, it will crash.

=item NET_ORDER

Writes the number in network order format and returns this number
acounting for this format.
If COUNTER is set, it adheres to the settings of that element.
Otherwise, user can set it. Defaults to false.

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

    inline void rmaction(Packet *);
    void push(int, Packet *);
#if HAVE_BATCH
    void push_batch(int, PacketBatch *);
#endif

    inline Timestamp get(uint64_t i);

    inline bool has_net_order() {
        return _net_order;
    }

    inline uint64_t get_numberpacket(Packet *p, int offset, bool net_order) {
        return _np ? _np->read_number_of_packet(p, offset, net_order) :
                     NumberPacket::read_number_of_packet(p, offset, net_order);
    }

private:
    int _offset;
    bool _dynamic;
    bool _net_order;
    Vector<Timestamp> _timestamps;
    NumberPacket *_np;
};

const Timestamp read_timestamp = Timestamp::make_sec(1);

inline Timestamp RecordTimestamp::get(uint64_t i) {
    if (i >= (unsigned)_timestamps.size()) {
        click_chatter("Out of index !");
        return Timestamp::uninitialized_t();
    }
    Timestamp t = _timestamps[i];
    if (t == read_timestamp) {
        click_chatter("Timestamp read multiple times !");
        return Timestamp::uninitialized_t();
    }
    _timestamps[i] = read_timestamp;
    return t;
}

CLICK_ENDDECLS

#endif
