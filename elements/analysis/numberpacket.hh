#ifndef CLICK_NUMBERPACKET_HH
#define CLICK_NUMBERPACKET_HH

#include <click/batchelement.hh>

CLICK_DECLS

/*
=c

NumberPacket()

=s

Set an increasing number inside the payload of each packets

=item OFFSET

Set an offset to write the data. Default to 40.

=a

RecordTimestamp, TimestampDiff */

class NumberPacket : public BatchElement {
public:
    NumberPacket() CLICK_COLD;
    ~NumberPacket() CLICK_COLD;

    const char *class_name() const { return "NumberPacket"; }
    const char *port_count() const { return PORTS_1_1; }
    const char *processing() const { return AGNOSTIC; }
    const char *flow_code() const { return "x/x"; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    Packet * simple_action(Packet *) override;
#if HAVE_BATCH
    PacketBatch * simple_action_batch(PacketBatch *) override;
#endif

    static inline uint64_t read_number_of_packet(const Packet *p,int offset) {
        return *(reinterpret_cast<const uint64_t *>(p->data() + offset));
    }

    void add_handlers();
private:
    atomic_uint32_t _count;
    int _offset;

    inline Packet* smaction(Packet* p) CLICK_WARN_UNUSED_RESULT;

    enum{ H_COUNT};
    static String read_handler(Element *e, void *thunk);
};

CLICK_ENDDECLS

#endif
