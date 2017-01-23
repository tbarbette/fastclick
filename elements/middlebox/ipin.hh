#ifndef MIDDLEBOX_IPIN_HH
#define MIDDLEBOX_IPIN_HH
#include "ipelement.hh"
#include <click/element.hh>

CLICK_DECLS

class IPIn : public IPElement
{
public:
    IPIn() CLICK_COLD;

    const char *class_name() const        { return "IPIn"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push_batch(int, PacketBatch*) override;
protected:
    void setPacketModified(WritablePacket*) override;
};

CLICK_ENDDECLS
#endif
