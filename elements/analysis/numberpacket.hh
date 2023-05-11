#ifndef CLICK_NUMBERPACKET_HH
#define CLICK_NUMBERPACKET_HH

#include <click/glue.hh>
#include <click/batchelement.hh>

CLICK_DECLS

/*
=c

NumberPacket() Set an increasing number inside packet

=s analysis

=d

Set an increasing number inside the payload of each packet

=item OFFSET

Set an offset to write the data. Default to 40.

=item NET_ORDER

Writes the number in network order format and returns this number
acounting for this format. Defaults to false.

=a

RecordTimestamp, TimestampDiff */

class NumberPacket : public SimpleElement<NumberPacket> {
public:
    NumberPacket() CLICK_COLD;
    ~NumberPacket() CLICK_COLD;

    const char *class_name() const override { return "NumberPacket"; }
    const char *port_count() const override { return PORTS_1_1; }
    const char *processing() const override { return AGNOSTIC; }
    const char *flow_code() const override { return "x/x"; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    Packet *simple_action(Packet *) override;

    static inline uint64_t read_number_of_packet(
            const Packet *p, int offset, bool net_order = false) {
        if (net_order) {
            return htonll(*(reinterpret_cast<const uint64_t *>(p->data() + offset)));
        }
        return *(reinterpret_cast<const uint64_t *>(p->data() + offset));
    }

    inline bool has_net_order() {
        return _net_order;
    }

    void add_handlers();
private:
    atomic_uint64_t _count;
    int _offset;
    bool _net_order;
    const int _size_of_number = sizeof(uint64_t);

    inline Packet *smaction(Packet *p) CLICK_WARN_UNUSED_RESULT;

    enum{ H_COUNT};
    static String read_handler(Element *e, void *thunk);
};

CLICK_ENDDECLS

#endif
