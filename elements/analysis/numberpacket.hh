#ifndef CLICK_NUMBERPACKET_HH
#define CLICK_NUMBERPACKET_HH

#include <click/element.hh>

CLICK_DECLS

/*
 * TODO: documentation.
 */
class NumberPacket : public Element {
public:
    NumberPacket() CLICK_COLD;
    ~NumberPacket() CLICK_COLD;

    const char *class_name() const { return "NumberPacket"; }
    const char *port_count() const { return PORTS_1_1; }
    const char *processing() const { return PUSH; }
    const char *flow_code() const { return "x/x"; }
    int configure_phase() const { return CONFIGURE_PHASE_DEFAULT; }
    bool can_live_reconfigure() const { return false; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push(int, Packet *);

private:
    uint64_t _count;
};

constexpr unsigned HEADER_SIZE = 40; // Skip enough for TCP header

inline uint64_t read_number_of_packet(const Packet *p) {
    return *(reinterpret_cast<const uint64_t *>(p->data() + HEADER_SIZE));
}


CLICK_ENDDECLS

#endif
