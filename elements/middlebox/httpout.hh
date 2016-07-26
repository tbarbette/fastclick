#ifndef MIDDLEBOX_HTTPOUT_HH
#define MIDDLEBOX_HTTPOUT_HH
#include <click/element.hh>
#include "stackelement.hh"
#include "memorypool.hh"
#include "tcpelement.hh"
#include "flowbuffer.hh"
#include "flowbufferentry.hh"

CLICK_DECLS

#define POOL_BUFFER_ENTRIES_SIZE 300

class HTTPOut : public StackElement, public TCPElement
{
public:
    HTTPOut() CLICK_COLD;

    const char *class_name() const        { return "HTTPOut"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    bool isOutElement()                   { return true; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

protected:
    Packet* processPacket(struct fcb*, Packet*);
    WritablePacket* setHeaderContent(struct fcb *fcb, WritablePacket* packet,
        const char* headerName, const char* content) CLICK_WARN_UNUSED_RESULT;

    // Will be associated to the thread managing this direction of the flow as a HTTPOut
    // element is responsible for a direction of the flow and thus used by only one thread
    MemoryPool<struct flowBufferEntry> poolBufferEntries;
};

CLICK_ENDDECLS
#endif
