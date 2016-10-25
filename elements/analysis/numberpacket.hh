#ifndef CLICK_NUMBERPACKET_HH
#define CLICK_NUMBERPACKET_HH

#include <click/batchelement.hh>

CLICK_DECLS

/*
=c

NumberPacket()

=s

Set an increasing number inside the payload of each packets

=a

RecordTimestamp, TimestampDiff */

class NumberPacket : public BatchElement {
public:
    NumberPacket() CLICK_COLD;
    ~NumberPacket() CLICK_COLD;

    const char *class_name() const { return "NumberPacket"; }
    const char *port_count() const { return PORTS_1_1; }
    const char *processing() const { return PUSH; }
    const char *flow_code() const { return "x/x"; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push(int, Packet *);
#if HAVE_BATCH
    void push_batch(int, PacketBatch *);
#endif

private:
    uint64_t _count;

    inline Packet* smaction(Packet* p) CLICK_WARN_UNUSED_RESULT;
};

constexpr unsigned HEADER_SIZE = 40; // Skip enough for TCP header

inline uint64_t read_number_of_packet(const Packet *p) {
    return *(reinterpret_cast<const uint64_t *>(p->data() + HEADER_SIZE));
}


CLICK_ENDDECLS

#endif
