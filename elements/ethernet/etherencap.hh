#ifndef CLICK_ETHERENCAP_HH
#define CLICK_ETHERENCAP_HH
#include <click/element.hh>
#include <click/batchelement.hh>
#include <clicknet/ether.h>
CLICK_DECLS

/*
=c

EtherEncap(ETHERTYPE, SRC, DST)

=s ethernet

encapsulates packets in Ethernet header

=d

Encapsulates each packet in the Ethernet header specified by its arguments.
ETHERTYPE should be in host order.

=e

Encapsulate packets in an Ethernet header with type
ETHERTYPE_IP (0x0800), source address 1:1:1:1:1:1, and
destination address 2:2:2:2:2:2:

  EtherEncap(0x0800, 1:1:1:1:1:1, 2:2:2:2:2:2)

=n

For IP packets you probably want to use ARPQuerier instead.

=h src read/write

Return or set the SRC parameter.

=h dst read/write

Return or set the DST parameter.

=h ethertype read/write

Return or set the ETHERTYPE parameter.

=a

EtherVLANEncap, ARPQuerier, EnsureEther, StoreEtherAddress, EtherRewrite */


class EtherEncap : public BatchElement {
    public:

        EtherEncap() CLICK_COLD;
        ~EtherEncap() CLICK_COLD;

        const char *class_name() const override    { return "EtherEncap"; }
        const char *port_count() const override    { return PORTS_1_1; }

        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
        bool can_live_reconfigure() const    { return true; }
        void add_handlers() CLICK_COLD;

        inline Packet *smaction(Packet *);

        Packet *pull(int) override;
        void push      (int, Packet*) override;
    #if HAVE_BATCH
        void push_batch(int, PacketBatch*) override;
        PacketBatch *pull_batch(int,unsigned) override;
    #endif

    private:

        click_ether _ethh;
};

CLICK_ENDDECLS
#endif
