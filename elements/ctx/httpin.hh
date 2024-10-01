#ifndef MIDDLEBOX_HTTPIN_HH
#define MIDDLEBOX_HTTPIN_HH
#include <click/element.hh>
#include <click/flow/ctxelement.hh>
#include <click/tcphelper.hh>

CLICK_DECLS

/**
 * Structure used by the HTTPIn element
 */
struct fcb_httpin
{
    bool headerFound;
//    char url[2048];
//    char method[16];
    uint64_t contentLength;
    uint64_t contentSeen;
    int64_t contentRemoved;
    bool CLRemoved;
    bool KARemoved;
    bool isRequest;

    fcb_httpin()
    {
        headerFound = false;
        contentSeen = 0;
        contentLength = 0;
 //       url[0] = '\0';
//        method[0] = '\0';
//        isRequest = false;
        CLRemoved = 0;
    }
};


enum fill_method{RESIZE_FILL_END, RESIZE_FILL, RESIZE_CHUNKED, RESIZE_HTTP10};

/*
=c

HTTPIn()

=s ctx

entry point of an HTTP path in the stack of the middlebox

=d

This element is the entry point of an HTTP path in the stack of the middlebox by which all
HTTP packets must go before their HTTP content is processed. Each path containing an HTTPIn element
must also contain an HTTPOut element

=a HTTPOut */

class HTTPIn : public CTXSpaceElement<fcb_httpin>, TCPHelper
{
public:
    HTTPIn() CLICK_COLD;

    const char *class_name() const        { return "HTTPIn"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;


    virtual int maxModificationLevel(Element* stop) override;

    virtual void removeBytes(WritablePacket*, uint32_t, uint32_t) override;

    virtual WritablePacket* insertBytes(WritablePacket*, uint32_t, uint32_t) override;

    //Called by OUT when the request is finished to reset buffer for a new request (HTTP connections can be re-used)
    void requestTerminated();

    bool _set10;
    bool _remove_encoding;
    int _buffer;

    bool _resize;
    enum fill_method _fill;
protected:

    void push_flow(int port, fcb_httpin* fcb, PacketBatch* flow) override;

    virtual bool isLastUsefulPacket(Packet *packet);

private:
    bool _verbose;

    /** @brief Remove an HTTP header from a request or a response
     * @param fcb Pointer to the FCB of the flow
     * @param packet Packet in which the header is located
     * @param header Name of the header to remove
     * @return true if header was removed
     */
    bool removeHeader(WritablePacket* packet, const StringRef &header);

    /** @brief Return the content of an HTTP header
     * @param fcb Pointer to the FCB of the flow
     * @param packet Packet in which the header is located
     * @param headerName Name of the header to remove
     * @param buffer Buffer in which the content will be put
     * @param bufferSize Maximum size of the header (the content will be truncated if the buffer
     * is not large enough to contain it)
     */
    bool getHeaderContent(struct fcb_httpin *fcb, WritablePacket* packet, const StringRef &headerName,
        char* buffer, uint32_t bufferSize);

    /** @brief Process the headers and set the URL and the method in the httpin part of the FCB
     * @param fcb Pointer to the FCB of the flow
     * @param packet Packet in which the headers are located
     */
    void setRequestParameters(struct fcb_httpin *fcb, WritablePacket *packet);

    /** @brief Modify the HTTP version in the header to set it to 1.0
     * @param fcb Pointer to the FCB of the flow
     * @param packet Packet in which the headers are located
     * @return The packet with the HTTP version modified
     */
    WritablePacket* setHTTP10(struct fcb_httpin *fcb, WritablePacket *packet, int &endOfMethod) CLICK_WARN_UNUSED_RESULT;
};

CLICK_ENDDECLS
#endif
