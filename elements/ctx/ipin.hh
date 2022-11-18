#ifndef MIDDLEBOX_IPIN_HH
#define MIDDLEBOX_IPIN_HH
#include <click/ipelement.hh>
#include <click/flow/ctxelement.hh>
#include <click/element.hh>

CLICK_DECLS

/*
=c

IPIn()

=s ctx

entry point of an IP path in the stack of the middlebox

=d

This element is the entry point of an IP path in the stack of the middlebox by which all
IP packets must go before their IP content is processed. Each path containing a IPIn element
must also contain an IPOut element

=a IPOut */

class IPIn : public CTXElement, public IPElement
{
public:
    /** @brief Construct an IPIn element
     */
    IPIn() CLICK_COLD;

    const char *class_name() const        { return "IPIn"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    virtual FlowNode* resolveContext(FlowType t, Vector<FlowElement*> contextStack) override;

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push_batch(int, PacketBatch*) override;

    virtual FlowNode* get_table(int,Vector<FlowElement*> context) override;

    virtual FlowType getContext(int port);

};

CLICK_ENDDECLS
#endif
