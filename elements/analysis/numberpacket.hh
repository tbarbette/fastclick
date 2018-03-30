#ifndef CLICK_NUMBERPACKET_HH
#define CLICK_NUMBERPACKET_HH

#include <click/batchelement.hh>

#define TYPE_INIT 0
#define TYPE_LITEND 1
#define TYPE_BIGEND 2

CLICK_DECLS

/*
=c

NumberPacket()

=s

Set an increasing number inside the payload of each packets

=item OFFSET

Set an offset to write the data. Default to 40.

=item NET_ORDER

Writes the number in network order format and returns this number
acounting for this format. Defaults to false.

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

    Packet *simple_action(Packet *) override;
#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch *) override;
#endif

    static unsigned long long htonll(unsigned long long src) {
        static int typ = TYPE_INIT;
        unsigned char c;
        union {
            unsigned long long ull;
            unsigned char c[8];
        } x;

        if (typ == TYPE_INIT) {
            x.ull = 0x01;
            typ = (x.c[7] == 0x01ULL) ? TYPE_BIGEND : TYPE_LITEND;
        }

        if (typ == TYPE_BIGEND)
            return src;

        x.ull = src;
        c = x.c[0]; x.c[0] = x.c[7]; x.c[7] = c;
        c = x.c[1]; x.c[1] = x.c[6]; x.c[6] = c;
        c = x.c[2]; x.c[2] = x.c[5]; x.c[5] = c;
        c = x.c[3]; x.c[3] = x.c[4]; x.c[4] = c;

        return x.ull;
    }

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
