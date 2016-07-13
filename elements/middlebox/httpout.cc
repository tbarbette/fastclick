#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/string.hh>
#include "httpout.hh"

CLICK_DECLS

HTTPOut::HTTPOut() : poolBufferEntries(POOL_BUFFER_ENTRIES_SIZE)
{

}

int HTTPOut::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

Packet* HTTPOut::processPacket(struct fcb* fcb, Packet* p)
{
    WritablePacket *packet = p->uniqueify();

    assert(packet != NULL);

    if(!fcb->httpout.flowBuffer.isInitialized())
        fcb->httpout.flowBuffer.initialize(this, &poolBufferEntries);

    if(!isPacketContentEmpty(packet) && fcb->httpin.contentLength > 0)
    {
        FlowBuffer &flowBuffer = fcb->httpout.flowBuffer;
        flowBuffer.enqueue(packet);
        requestMorePackets(fcb, packet);

        // We have the whole content
        if(isLastUsefulPacket(fcb, packet))
        {
            // Compute the new Content-Length
            FlowBufferIter it = flowBuffer.begin();

            uint64_t newContentLength = 0;
            while(it != flowBuffer.end())
            {
                newContentLength += getPacketContentSize(*it);
                ++it;
            }

            WritablePacket *toPush = flowBuffer.dequeue();;

            char bufferHeader[25];

            sprintf(bufferHeader, "%lu", newContentLength);
            toPush = setHeaderContent(fcb, toPush, "Content-Length", bufferHeader);

            click_chatter("Content-Length modified to %lu", newContentLength);

            while(toPush != NULL)
            {
                output(0).push(toPush);
                toPush = flowBuffer.dequeue();
            }
        }

        return NULL;
    }

    return packet;
}

WritablePacket* HTTPOut::setHeaderContent(struct fcb *fcb, WritablePacket* packet, const char* headerName, const char* content)
{
    unsigned char* source = getPayload(packet);
    unsigned char* beginning = (unsigned char*)strstr((char*)source, headerName);

    if(beginning == NULL)
        return packet;

    beginning += strlen(headerName) + 1;

    unsigned char* end = (unsigned char*)strstr((char*)beginning, "\r\n");
    if(end == NULL)
        return packet;

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
        packet = insertBytes(fcb, packet, prevEndPos, offset);
    else if(offset < 0)
        removeBytes(fcb, packet, endPos, -offset);

    memcpy(beginning, content, newSize);

    return packet;
}


CLICK_ENDDECLS
EXPORT_ELEMENT(HTTPOut)
ELEMENT_REQUIRES(FlowBuffer)
//ELEMENT_MT_SAFE(HTTPOut)
