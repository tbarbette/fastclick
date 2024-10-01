#ifndef MIDDLEBOX_IPOUT_HH
#define MIDDLEBOX_IPOUT_HH
#include <click/element.hh>
#include <click/ipelement.hh>
#include <click/flow/ctxelement.hh>

CLICK_DECLS

/*
=c

IPOut()

=s ctx

exit point of an IP path in the stack of the middlebox

=d

This element is the exit point of an IP path in the stack of the middlebox by which all
IP packets must go after their IP content has been processed. Each path containing a IPOut element
must also contain an IPIn element

=a IPIn */

class IPOut : public CTXElement, public IPElement
{
public:
    /** @brief Construct an IPOut element
     */
    IPOut() CLICK_COLD;

    const char *class_name() const        { return "IPOut"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;

    void push_batch(int, PacketBatch*) override;
protected:
    bool _readonly;
    bool _checksum;
};

CLICK_ENDDECLS
#endif
