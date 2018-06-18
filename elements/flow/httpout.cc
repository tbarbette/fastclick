/*
 * httpout.{cc,hh} -- exit point of an HTTP path in the stack of the middlebox
 * Romain Gaillard
 * Tom Barbette
 *
 */

#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/string.hh>
#include "httpout.hh"
#include "httpin.hh"

CLICK_DECLS

HTTPOut::HTTPOut()
{
    #if HAVE_BATCH
        in_batch_mode = BATCH_MODE_YES;
    #endif
}

int HTTPOut::configure(Vector<String> &conf, ErrorHandler *errh)
{
        ElementCastTracker visitor(router(),"HTTPIn");
        router()->visit_upstream(this, -1, &visitor);
        if (visitor.size() != 1) {
            return errh->error("Found no or more than 1 HTTPIn element. Specify which one to use with OUTNAME");
        }
        _in = static_cast<HTTPIn*>(visitor[0]);

    return 0;
}

void HTTPOut::push_batch(int, struct fcb_httpout* fcb, PacketBatch* flow)
{
    auto fnt = [this,fcb](Packet*p) -> Packet* {
    /*
        // Initialize the buffer if not already done
        if(!fcb->flowBuffer.isInitialized())
            fcb->flowBuffer.initialize();*/

        // Check that the packet contains HTTP content
        if(!p->isPacketContentEmpty() && _in->fcb_data()->contentLength > 0)
        {
            WritablePacket *packet = p->uniqueify();

            assert(packet != NULL);
            FlowBuffer &flowBuffer = fcb->flowBuffer;
            flowBuffer.enqueue(packet);
            requestMorePackets(packet);

            // Check if we have the whole content in the buffer
            if(isLastUsefulPacket(packet))
            {
                // Compute the new Content-Length
                FlowBufferIter it = flowBuffer.begin();

                uint64_t newContentLength = 0;
                while(it != flowBuffer.end())
                {
                    newContentLength += (*it)->getPacketContentSize();
                    ++it;
                }

                WritablePacket *toPush = (WritablePacket*)flowBuffer.dequeue();

                char bufferHeader[25];

                sprintf(bufferHeader, "%lu", newContentLength);
                toPush = setHeaderContent(fcb, toPush, "Content-Length", bufferHeader);

                // Flush the buffer

                PacketBatch *headBatch = PacketBatch::make_from_packet(toPush);

                PacketBatch *tailBatch = NULL;
                uint32_t max = flowBuffer.getSize();
                MAKE_BATCH(flowBuffer.dequeue(), tailBatch, max);

                if(headBatch != NULL)
                {
                    // Join the two batches
                    if(tailBatch != NULL)
                        headBatch->append_batch(tailBatch);
                    output_push_batch(0, headBatch);
                }

            }

            return NULL;
        }
        else
            return p;
    };

    EXECUTE_FOR_EACH_PACKET_DROPPABLE(fnt, flow, [](Packet*){});
}

WritablePacket* HTTPOut::setHeaderContent(struct fcb_httpout *fcb, WritablePacket* packet,
    const char* headerName, const char* content)
{
    unsigned char* source = getPayload(packet);

    // We set the content pointer to the TCP payload as we want to manipulate HTTP headers
    // and the current content pointer is set to the HTTP payload
    uint16_t offsetTcp = getPayloadOffset(packet);
    packet->setContentOffset(offsetTcp);

    unsigned char* beginning = (unsigned char*)searchInContent((char*)source, headerName,
        getPayloadLength(packet));

    if(beginning == NULL)
        return packet;

    beginning += strlen(headerName) + 1;

    uint32_t lengthLeft = getPayloadLength(packet) - (beginning - source);

    unsigned char* end = (unsigned char*)searchInContent((char*)beginning, "\r\n", lengthLeft);
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
        packet = insertBytes(packet, prevEndPos, offset);
    else if(offset < 0)
        removeBytes(packet, endPos, -offset);

    memcpy(beginning, content, newSize);

    return packet;
}


CLICK_ENDDECLS
EXPORT_ELEMENT(HTTPOut)
//ELEMENT_MT_SAFE(HTTPOut)
