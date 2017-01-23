#ifndef MIDDLEBOX_HTTPIN_HH
#define MIDDLEBOX_HTTPIN_HH
#include <click/element.hh>
#include "stackelement.hh"

CLICK_DECLS

class fcb_httpin
{
public:
    bool headerFound;

    fcb_httpin()
    {
        headerFound = false;
    }
};

class HTTPIn : public StackBufferElement<fcb_httpin>
{
public:
    HTTPIn() CLICK_COLD;

    const char *class_name() const        { return "HTTPIn"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push_batch(int port, fcb_httpin* fcb, PacketBatch* flow) override;

protected:
    void removeHeader(WritablePacket*, const char*);
};

CLICK_ENDDECLS
#endif
