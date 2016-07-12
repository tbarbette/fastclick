#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include "httpin.hh"

CLICK_DECLS

HTTPIn::HTTPIn()
{

}

int HTTPIn::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

Packet* HTTPIn::processPacket(struct fcb *fcb, Packet* p)
{
    WritablePacket *packet = p->uniqueify();
    if(!fcb->httpin.headerFound && !isPacketContentEmpty(packet))
    {
        // Remove the "Accept-Encoding" header to avoid receiving
        // compressed content
        setHTTP10(fcb, packet);
        removeHeader(fcb, packet, "Accept-Encoding");
        char buffer[250];
        getHeaderContent(fcb, packet, "Content-Length", buffer, 250);
        fcb->httpin.contentLength = (uint64_t)atol(buffer);

        if(fcb->httpin.contentLength > 0)
            click_chatter("Content-Length: %lu", fcb->httpin.contentLength);
    }

    // Compute the offset of the HTML payload
    const char* source = strstr((const char*)getPacketContentConst(packet), "\r\n\r\n");
    if(source != NULL)
    {
        uint32_t offset = (int)(source - (char*)packet->data() + 4);
        setContentOffset(packet, offset);
        fcb->httpin.headerFound = true;
    }

    return packet;
}

void HTTPIn::removeHeader(struct fcb *fcb, WritablePacket* packet, const char* header)
{
    unsigned char* source = getPacketContent(packet);
    unsigned char* beginning = (unsigned char*)strstr((char*)source, header);

    if(beginning == NULL)
        return;

    unsigned char* end = (unsigned char*)strstr((char*)beginning, "\r\n");
    if(end == NULL)
        return;

    unsigned nbBytesToRemove = (end - beginning) + strlen("\r\n");

    uint32_t position = beginning - source;

    removeBytes(fcb, packet, position, nbBytesToRemove);

    click_chatter("Removed header: %s", header);
    setPacketDirty(fcb, packet);
}

void HTTPIn::getHeaderContent(struct fcb *fcb, WritablePacket* packet, const char* headerName, char* buffer, uint32_t bufferSize)
{
    unsigned char* source = getPacketContent(packet);
    unsigned char* beginning = (unsigned char*)strstr((char*)source, headerName);

    if(beginning == NULL)
    {
        buffer[0] = '\0';
        return;
    }

    beginning += strlen(headerName) + 1;

    unsigned char* end = (unsigned char*)strstr((char*)beginning, "\r\n");
    if(end == NULL)
    {
        buffer[0] = '\0';
        return;
    }

    // Skip spaces at the beginning of the string
    while(beginning < end && beginning[0] == ' ')
        beginning++;

    uint16_t contentSize = end - beginning;

    if(contentSize >= bufferSize)
    {
        contentSize = bufferSize - 1;
        click_chatter("Warning: buffer not big enough to contain the header %s", headerName);
    }

    memcpy(buffer, beginning, contentSize);
    buffer[contentSize] = '\0';
}

void HTTPIn::setHeaderContent(struct fcb *fcb, WritablePacket* packet, const char* headerName, const char* content)
{
    unsigned char* source = getPacketContent(packet);
    unsigned char* beginning = (unsigned char*)strstr((char*)source, headerName);

    if(beginning == NULL)
        return;

    beginning += strlen(headerName) + 1;

    unsigned char* end = (unsigned char*)strstr((char*)beginning, "\r\n");
    if(end == NULL)
        return;

    // Skip spaces at the beginning of the string
    while(beginning < end && beginning[0] == ' ')
        beginning++;

    uint32_t startPos = beginning - source;
    uint32_t newSize = strlen(content);
    uint32_t endPos = startPos + newSize;
    uint32_t prevSize = end - beginning;
    uint32_t prevEndPos = startPos + prevSize;
    int offset = newSize - prevSize;

    // Ensure that the header has the right size
    if(offset > 0)
        insertBytes(fcb, packet, prevEndPos, offset);
    else if(offset < 0)
        removeBytes(fcb, packet, endPos, -offset);

    memcpy(beginning, content, newSize);
}

void HTTPIn::setHTTP10(struct fcb *fcb, WritablePacket *packet)
{
    unsigned char* source = getPacketContent(packet);
    unsigned char* endFirstLine = (unsigned char*)strstr((char*)source, "\r\n");

    if(endFirstLine == NULL)
        return;

    unsigned char* beginning = (unsigned char*)strstr((char*)source, "HTTP/");

    if(beginning == NULL || beginning > endFirstLine)
        return;

    // Ensure the line has the right length
    int offset = endFirstLine - beginning - 8; // 8 is the length of "HTTP/1.1"
    if(offset > 0)
        removeBytes(fcb, packet, beginning - source + 8, offset);
    else
        insertBytes(fcb, packet , beginning - source + 5, -offset);

    beginning[5] = '1';
    beginning[6] = '.';
    beginning[7] = '0';
}

void HTTPIn::setRequestParameters(struct fcb *fcb, WritablePacket *packet)
{
    /*
    unsigned char* source = getPacketContent(packet);
    unsigned char* requestEnd = (unsigned char*)strstr((char*)beginning, "\r\n");

    if(requestEnd == NULL)
        return;

    unsigned char* urlStart = (unsigned char*)strstr((char*)beginning, "\r\n");
    */
}

CLICK_ENDDECLS
EXPORT_ELEMENT(HTTPIn)
ELEMENT_REQUIRES(FlowBuffer)
//ELEMENT_MT_SAFE(HTTPIn)
