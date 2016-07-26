/*
 * httpin.{cc,hh} -- entry point of a HTTP path in the stack of the middlebox
 * Romain Gaillard
 *
 */

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

    // Check that the packet contains HTTP content
    if(isPacketContentEmpty(packet))
        return packet;

    // By default, the packet is not considered to be the last one
    // containing HTTP content
    setAnnotationLastUseful(packet, false);

    if(!fcb->httpin.headerFound)
    {
        // Remove the "Accept-Encoding" header to avoid receiving
        // compressed content
        packet = setHTTP10(fcb, packet);
        setRequestParameters(fcb, packet);
        removeHeader(fcb, packet, "Accept-Encoding");
        char buffer[250];
        getHeaderContent(fcb, packet, "Content-Length", buffer, 250);
        fcb->httpin.contentLength = (uint64_t)atol(buffer);
    }

    // Compute the offset of the HTML payload
    const char* source = strstr((const char*)getPacketContentConst(packet), "\r\n\r\n");
    if(source != NULL)
    {
        uint32_t offset = (int)(source - (char*)packet->data() + 4);
        setContentOffset(packet, offset);
        fcb->httpin.headerFound = true;
    }

    // Add the size of the HTTP content to the counter
    uint16_t currentContent = getPacketContentSize(packet);
    fcb->httpin.contentSeen += currentContent;
    // Check if we have seen all the HTTP content
    if(fcb->httpin.contentSeen >= fcb->httpin.contentLength)
        setAnnotationLastUseful(packet, true);

    return packet;
}


void HTTPIn::removeHeader(struct fcb *fcb, WritablePacket* packet, const char* header)
{
    unsigned char* source = getPayload(packet);

    // Search the header name
    unsigned char* beginning = (unsigned char*)strstr((char*)source, header);

    if(beginning == NULL)
        return;

    // Search the end of the header
    unsigned char* end = (unsigned char*)strstr((char*)beginning, "\r\n");
    if(end == NULL)
        return;

    // Compute the size of the header
    unsigned nbBytesToRemove = (end - beginning) + strlen("\r\n");

    uint32_t position = beginning - source;

    // Remove data corresponding to the header
    removeBytes(fcb, packet, position, nbBytesToRemove);
}

void HTTPIn::getHeaderContent(struct fcb *fcb, WritablePacket* packet, const char* headerName,
     char* buffer, uint32_t bufferSize)
{
    unsigned char* source = getPayload(packet);
    // Search the header name
    unsigned char* beginning = (unsigned char*)strstr((char*)source, headerName);

    if(beginning == NULL)
    {
        buffer[0] = '\0';
        return;
    }

    // Skip the colon
    beginning += strlen(headerName) + 1;

    // Search the end of the header
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

WritablePacket* HTTPIn::setHTTP10(struct fcb *fcb, WritablePacket *packet)
{
    unsigned char* source = getPayload(packet);
    // Search the end of the first line
    unsigned char* endFirstLine = (unsigned char*)strstr((char*)source, "\r\n");

    if(endFirstLine == NULL)
        return packet;

    // Search the HTTP version
    unsigned char* beginning = (unsigned char*)strstr((char*)source, "HTTP/");

    if(beginning == NULL || beginning > endFirstLine)
        return packet;

    // Ensure the line has the right length
    int offset = endFirstLine - beginning - 8; // 8 is the length of "HTTP/1.1"
    if(offset > 0)
        removeBytes(fcb, packet, beginning - source + 8, offset);
    else
        packet = insertBytes(fcb, packet , beginning - source + 5, -offset);

    beginning[5] = '1';
    beginning[6] = '.';
    beginning[7] = '0';

    return packet;
}

void HTTPIn::setRequestParameters(struct fcb *fcb, WritablePacket *packet)
{

    unsigned char* source = getPayload(packet);
    // Search the end of the first line
    unsigned char* endFirstLine = (unsigned char*)strstr((char*)source, "\r\n");

    if(endFirstLine == NULL)
        return;

    // Search the beginning of the URL (after the first space)
    unsigned char* urlStart = (unsigned char*)strstr((char*)source, " ");

    if(urlStart == NULL || urlStart >= endFirstLine)
        return;

    // Get the HTTP method (before the URL)
    uint16_t methodLength = urlStart - source;
    if(methodLength >= 16)
        methodLength = 15;

    memcpy(fcb->httpin.method, source, methodLength);
    fcb->httpin.method[methodLength] = '\0';

    // Search the end of the URL (before the second space)
    unsigned char* urlEnd = (unsigned char*)strstr((char*)(urlStart + 1), " ");

    if(urlEnd == NULL || urlEnd >= endFirstLine)
        return;

    uint16_t urlLength = urlEnd - urlStart - 1;

    if(urlLength >= 2048)
        urlLength = 2047;

    memcpy(fcb->httpin.url, urlStart + 1, urlLength);
    fcb->httpin.url[urlLength] = '\0';

    fcb->httpin.isRequest = true;
}


bool HTTPIn::isLastUsefulPacket(struct fcb* fcb, Packet *packet)
{
    return getAnnotationLastUseful(packet);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(HTTPIn)
ELEMENT_MT_SAFE(HTTPIn)
