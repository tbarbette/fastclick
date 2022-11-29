#ifndef MIDDLEBOX_HTTPOUT_HH
#define MIDDLEBOX_HTTPOUT_HH

#include <click/element.hh>
#include <click/flow/ctxelement.hh>
#include <click/tcphelper.hh>

CLICK_DECLS

class HTTPIn;


/**
 * Structure used by the HTTPIn element
 */
struct fcb_httpout
{
    FlowBuffer flowBuffer;
    long unsigned seen;
};


/*
=c

HTTPOut()

=s ctx

exit point of an HTTP path in the stack of the middlebox

=d

This element is the exit point of an HTTP path in the stack of the middlebox by which all
HTTP packets must go after their HTTP content has been processed. Each path containing an HTTPOut
element must also contain an HTTPIn element

=a HTTPIn */
/**
 * Structure used by the HTTPOut element
 */


class HTTPOut : public CTXSpaceElement<struct fcb_httpout>, public TCPHelper
{
public:
    /** @brief Construct an HTTPOut element
     */
    HTTPOut() CLICK_COLD;

    const char *class_name() const        { return "HTTPOut"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;

    virtual int maxModificationLevel(Element* stop) override;

    int initialize_http(ErrorHandler *) CLICK_COLD;

    void push_flow(int, struct fcb_httpout*, PacketBatch*) override;
protected:
    /** @brief Modify the content of an HTTP header
     * @param fcb Pointer to the FCB of the flow
     * @param packet Packet in which the header is located
     * @param headerName Name of the header to modify
     * @param content New content of the header
     * @return The packet with the HTTP header modified
     */
    WritablePacket* setHeaderContent(struct fcb_httpout *fcb, WritablePacket* packet,
        const StringRef &headerName, const StringRef &content) CLICK_WARN_UNUSED_RESULT;

    HTTPIn* _in;
};

CLICK_ENDDECLS
#endif
