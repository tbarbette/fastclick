#ifndef MIDDLEBOX_HTTPOUT_HH
#define MIDDLEBOX_HTTPOUT_HH
#include <click/element.hh>
#include "stackelement.hh"

CLICK_DECLS

/*
=c

HTPPOut()

=s middlebox

exit point of an HTTP path in the stack of the middlebox

=d

This element is the exit point of an HTTP path in the stack of the middlebox by which all
HTTP packets must go after their HTTP content has been processed. Each path containing an HTTPOut
element must also contain an HTTPIn element

=a HTTPIn */
/**
 * Structure used by the HTTPOut element
 */
struct fcb_httpout
{
    FlowBuffer flowBuffer;
};



#define POOL_BUFFER_ENTRIES_SIZE 300

class HTTPOut : public StackElement, public TCPElement
{
public:
    /** @brief Construct an HTTPOut element
     */
    HTTPOut() CLICK_COLD;

    const char *class_name() const        { return "HTTPOut"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PUSH; }

    bool isOutElement()                   { return true; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push_batch(int, PacketBatch*) override;
protected:
    /** @brief Modify the content of an HTTP header
     * @param fcb Pointer to the FCB of the flow
     * @param packet Packet in which the header is located
     * @param headerName Name of the header to modify
     * @param content New content of the header
     * @return The packet with the HTTP header modified
     */
    WritablePacket* setHeaderContent(struct fcb *fcb, WritablePacket* packet,
        const char* headerName, const char* content) CLICK_WARN_UNUSED_RESULT;

    per_thread<MemoryPool<struct flowBufferEntry>> poolBufferEntries;
};

CLICK_ENDDECLS
#endif
