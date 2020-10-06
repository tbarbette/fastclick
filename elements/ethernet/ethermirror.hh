#ifndef CLICK_ETHERMIRROR_HH
#define CLICK_ETHERMIRROR_HH
#include <click/batchelement.hh>
CLICK_DECLS

/*
 * =c
 * EtherMirror()
 * =s ethernet
 * swaps Ethernet source and destination
 * =d
 *
 * Incoming packets are Ethernet. Their source and destination Ethernet
 * addresses are swapped before they are output.
 * */

class EtherMirror : public BatchElement {
    public:

        EtherMirror() CLICK_COLD;
        ~EtherMirror() CLICK_COLD;

        const char *class_name() const override    { return "EtherMirror"; }
        const char *port_count() const override    { return PORTS_1_1; }

        Packet      *simple_action      (Packet *);
    #if HAVE_BATCH
        PacketBatch *simple_action_batch(PacketBatch *);
    #endif
};

CLICK_ENDDECLS
#endif // CLICK_ETHERMIRROR_HH
