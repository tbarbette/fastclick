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

CLICK_DECLS

HTTPOut::HTTPOut()
{
    #if HAVE_BATCH
        in_batch_mode = BATCH_MODE_YES;
    #endif

    // Initialize the memory pool of each thread
    for(unsigned int i = 0; i < poolBufferEntries.size(); ++i)
        poolBufferEntries.get_value(i).initialize(POOL_BUFFER_ENTRIES_SIZE);
}

int HTTPOut::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

void HTTPOut::push_batch(int, PacketBatch* flow)
{
	FOR_EACH_PACKET(flow,p) {
	    WritablePacket *packet = p->uniqueify();

	    assert(packet != NULL);

	    // Initialize the buffer if not already done
	    if(!fcb->httpout.flowBuffer.isInitialized())
		fcb->httpout.flowBuffer.initialize(this, &(*poolBufferEntries));

	    // Check that the packet contains HTTP content
	    if(!isPacketContentEmpty(packet) && fcb->httpin.contentLength > 0)
	    {
		FlowBuffer &flowBuffer = fcb->httpout.flowBuffer;
		flowBuffer.enqueue(packet);
		requestMorePackets(fcb, packet);

		// Check if we have the whole content in the buffer
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

		    WritablePacket *toPush = flowBuffer.dequeue();

		    char bufferHeader[25];

		    sprintf(bufferHeader, "%lu", newContentLength);
		    toPush = setHeaderContent(fcb, toPush, "Content-Length", bufferHeader);

		    // Flush the buffer
		    #if HAVE_BATCH
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
		    #else
		        while(toPush != NULL)
		        {
		            output(0).push(toPush);
		            toPush = flowBuffer.dequeue();
		        }
		    #endif
		}

		return NULL;
	    }

	}
    output(0).push_batch(flow);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(HTTPOut)
//ELEMENT_MT_SAFE(HTTPOut)
