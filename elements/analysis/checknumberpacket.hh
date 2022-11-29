#ifndef CLICK_CHECKNUMBERPACKET_HH
#define CLICK_CHECKNUMBERPACKET_HH

#include <click/batchelement.hh>
#include "numberpacket.hh"

CLICK_DECLS

/*
=c

CheckNumberPacket() Check increasing number inside packet

=s analysis

=d

Check there is an increasing number inside the payload of each packets

=item OFFSET

Set an offset to write the data. Default to 40.

=item NET_ORDER

Writes the number in network order format and returns this number
acounting for this format. Defaults to false.

=a

RecordTimestamp, TimestampDiff */

class CheckNumberPacket : public BatchElement {
public:
    CheckNumberPacket() CLICK_COLD;
    ~CheckNumberPacket() CLICK_COLD;

    const char *class_name() const override { return "CheckNumberPacket"; }
    const char *port_count() const override { return PORTS_1_1X2; }
    const char *processing() const override { return PUSH; }
    const char *flow_code() const override { return "x/x"; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push(int,Packet *) override;
#if HAVE_BATCH
    void push_batch(int,PacketBatch *) override;
#endif

    static inline uint64_t read_number_of_packet(
            const Packet *p, int offset, bool net_order = false) {
        return NumberPacket::read_number_of_packet(p, offset, net_order);
    }

    void add_handlers();
private:
    int _offset;
    bool _net_order;
    uint64_t _count;
    Vector<int> _numbers;

    enum{H_MAX, H_MIN, H_COUNT, H_DUMP};
    static int write_handler(const String &s_in, Element *e, void *thunk, ErrorHandler *errh);
    static String read_handler(Element *e, void *thunk);

    inline int smaction(Packet* p) CLICK_WARN_UNUSED_RESULT;


};

CLICK_ENDDECLS

#endif
