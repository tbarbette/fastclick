#ifndef MIDDLEBOX_HTTPIN_HH
#define MIDDLEBOX_HTTPIN_HH
#include <click/element.hh>
#include "stackelement.hh"

CLICK_DECLS

class HTTPIn : public StackElement
{
public:
    HTTPIn() CLICK_COLD;

    const char *class_name() const        { return "HTTPIn"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

protected:
    Packet* processPacket(struct fcb*, Packet*);
    void removeHeader(struct fcb *fcb, WritablePacket*, const char*);
    void getHeaderContent(struct fcb *fcb, WritablePacket* packet, const char* headerName, char* buffer, uint32_t bufferSize);
    void setHeaderContent(struct fcb *fcb, WritablePacket* packet, const char* headerName, const char* content);
    void setRequestParameters(struct fcb *fcb, WritablePacket *packet);
};

CLICK_ENDDECLS
#endif
