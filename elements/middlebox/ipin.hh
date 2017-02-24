#ifndef MIDDLEBOX_IPIN_HH
#define MIDDLEBOX_IPIN_HH
#include "ipelement.hh"
#include "stackelement.hh"
#include <click/element.hh>

CLICK_DECLS

/*
=c

IPIn()

=s middlebox

entry point of an IP path in the stack of the middlebox

=d

This element is the entry point of an IP path in the stack of the middlebox by which all
IP packets must go before their IP content is processed. Each path containing a IPIn element
must also contain an IPOut element

=a IPOut */

class IPIn : public StackElement, public IPElement
{
public:
    /** @brief Construct an IPIn element
     */
    IPIn() CLICK_COLD;

    const char *class_name() const        { return "IPIn"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push_batch(int, PacketBatch*) override;
};

CLICK_ENDDECLS
#endif
