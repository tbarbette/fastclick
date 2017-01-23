#ifndef MIDDLEBOX_IPOUT_HH
#define MIDDLEBOX_IPOUT_HH
#include <click/element.hh>
#include "ipelement.hh"

CLICK_DECLS

class IPOut : public IPElement
{
public:
    IPOut() CLICK_COLD;

    const char *class_name() const        { return "IPOut"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    bool isOutElement()             { return true; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push_batch(int, PacketBatch*) override;
protected:
};

CLICK_ENDDECLS
#endif
