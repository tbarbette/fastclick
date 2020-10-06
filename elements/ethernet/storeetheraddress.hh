#ifndef CLICK_STOREETHERADDRESS_HH
#define CLICK_STOREETHERADDRESS_HH
#include <click/batchelement.hh>
#include <click/etheraddress.hh>
CLICK_DECLS

/*
=c

StoreEtherAddress(ADDR, OFFSET)

=s ethernet

stores Ethernet address in packet

=d

Writes an Ethernet address ADDR into the packet at offset OFFSET.  If OFFSET
is out of range, the input packet is dropped or emitted on optional output 1.

The OFFSET argument may be 'src' or 'dst'.  These strings are equivalent to
offsets 6 and 0, respectively, which are the offsets into an Ethernet header
of the source and destination Ethernet addresses.

=h addr read/write

Return or set the ADDR argument.

=a

EtherEncap
*/

class StoreEtherAddress : public BatchElement { public:

    const char *class_name() const override { return "StoreEtherAddress"; }
    const char *port_count() const override { return PORTS_1_1X2; }
    const char *processing() const override { return PROCESSING_A_AH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    void add_handlers() CLICK_COLD;

    Packet      *simple_action      (Packet      *p);
#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch *batch);
#endif

 private:

    uint32_t _offset;
    bool _use_anno;
    uint8_t _anno;
    EtherAddress _address;

};

CLICK_ENDDECLS
#endif
