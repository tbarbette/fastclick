#ifndef CLICK_CHECKNUMBERPACKET_HH
#define CLICK_CHECKNUMBERPACKET_HH

#include <click/batchelement.hh>

CLICK_DECLS

/*
=c

CheckNumberPacket()

=s

Check there is an increasing number inside the payload of each packets

=item OFFSET

Set an offset to write the data. Default to 40.

=a

RecordTimestamp, TimestampDiff */

class CheckNumberPacket : public BatchElement {
public:
    CheckNumberPacket() CLICK_COLD;
    ~CheckNumberPacket() CLICK_COLD;

    const char *class_name() const { return "CheckNumberPacket"; }
    const char *port_count() const { return PORTS_1_1X2; }
    const char *processing() const { return PUSH; }
    const char *flow_code() const { return "x/x"; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push(int,Packet *) override;
#if HAVE_BATCH
    void push_batch(int,PacketBatch *) override;
#endif

    static inline uint64_t read_number_of_packet(const Packet *p, int offset) {
        return *(reinterpret_cast<const uint64_t *>(p->data() + offset));
    }


    void add_handlers();
private:
    int _offset;
    int _count;
    Vector<int> _numbers;

    enum{H_MAX, H_MIN, H_COUNT, H_DUMP};
    static int write_handler(const String &s_in, Element *e, void *thunk, ErrorHandler *errh);
    static String read_handler(Element *e, void *thunk);

    inline int smaction(Packet* p) CLICK_WARN_UNUSED_RESULT;


};

CLICK_ENDDECLS

#endif
