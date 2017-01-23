#ifndef MIDDLEBOX_HTTPOUT_HH
#define MIDDLEBOX_HTTPOUT_HH
#include <click/element.hh>
#include "stackelement.hh"

CLICK_DECLS

class HTTPOut : public StackElement
{
public:
    HTTPOut() CLICK_COLD;

    const char *class_name() const        { return "HTTPOut"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    bool isOutElement()                   { return true; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push_batch(int, PacketBatch*) override;

};

CLICK_ENDDECLS
#endif
