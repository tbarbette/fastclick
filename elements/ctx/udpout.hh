#ifndef MIDDLEBOX_UDPOut_HH
#define MIDDLEBOX_UDPOut_HH
#include <click/ipelement.hh>
#include <click/flow/ctxelement.hh>
#include <click/element.hh>

CLICK_DECLS

/*
=c

UDPOut()

=s ctx

entry point of an IP path in the stack of the middlebox

=d

This element is the entry point of an IP path in the stack of the middlebox by which all
IP packets must go before their IP content is processed. Each path containing a UDPOut element
must also contain an IPOut element

=a IPOut */

class UDPOut : public CTXElement, public IPElement
{
public:
    /** @brief Construct an UDPOut element
     */
    UDPOut() CLICK_COLD;

    const char *class_name() const        { return "UDPOut"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;

    void push_batch(int, PacketBatch*) override;
};

CLICK_ENDDECLS
#endif
