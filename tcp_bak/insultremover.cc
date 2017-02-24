/*
 * insultremover.{cc,hh} -- remove insults in web pages
 * Romain Gaillard
 * Tom Barbette
 */

#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include "insultremover.hh"
#include "tcpelement.hh"

CLICK_DECLS

InsultRemover::InsultRemover()
{
    #if HAVE_BATCH
        in_batch_mode = BATCH_MODE_YES;
    #endif

    // Initialize the memory pool of each thread
    for(unsigned int i = 0; i < poolBufferEntries.size(); ++i)
        poolBufferEntries.get_value(i).initialize(POOL_BUFFER_ENTRIES_SIZE);

    closeAfterInsults = false;
    closed = false;
}

int InsultRemover::configure(Vector<String> &conf, ErrorHandler *errh)
{
    // Build the list of "insults"
    insults.push_back("and");
    insults.push_back("astronomical");

    if(Args(conf, this, errh)
    .read_p("CLOSECONNECTION", closeAfterInsults)
    .complete() < 0)
        return -1;

    return 0;
}

void InsultRemover::push_batch(int port, PacketBatch* flow)
{
    FOR_EACH_PACKET_SAFE(flow, p) {
	    WritablePacket *packet = p->uniqueify();
	    assert(packet != NULL);

	    if(!fcb->insultremover.flowBuffer.isInitialized())
		fcb->insultremover.flowBuffer.initialize(this, &(*poolBufferEntries));

	    if(closed)
		return packet;

	    if(isPacketContentEmpty(packet))
		return packet;

	    FlowBuffer &flowBuffer = fcb->insultremover.flowBuffer;
	    flowBuffer.enqueue(packet);

	    bool needMorePackets = false;

	    for(int i = 0; i < insults.size(); ++i)
	    {
		int result = removeInsult(fcb, insults.at(i));
		if(result == 0)
		    needMorePackets = true;
	    }

	    // If the parameter "CLOSECONNECTION" is set and if insults have been found
	    // we close the connection after this packet and we replace its content by an error message
	    if(closeAfterInsults && fcb->insultremover.counterRemoved > 0)
	    {
		closed = true;
		closeConnection(fcb, packet, true, true);
		removeBytes(fcb, packet, 0, getPacketContentSize(packet));
		const char *message = "<font color='red'>The web page contains insults and has been "
		    "blocked</font><br />";
		packet = insertBytes(fcb, packet, 0, strlen(message) + 1);
		strcpy((char*)getPacketContent(packet), message);
	    }

	    // If the beginning of an insult could be found at the end of a packet
	    // we keep it in the buffer. Otherwise, we flush the buffer as we know
	    // that we will not be able to find a part of an insult in it
	    // Of course, if we have the last packet of the flow, we know
	    // that it is useless to buffer it until the next one.
	    if(!isLastUsefulPacket(fcb, packet) && needMorePackets && !closed)
		requestMorePackets(fcb, packet);
	    else
	    {
		// Otherwise, we flush the buffer
		#if HAVE_BATCH
		    PacketBatch *batch = NULL;
		    uint32_t max = flowBuffer.getSize();
		    MAKE_BATCH(flowBuffer.dequeue(), batch, max);

		    if(batch != NULL)
		        output_push_batch(0, batch);
		#else
		    WritablePacket *toPush = flowBuffer.dequeue();
		    while(toPush != NULL)
		    {
		        output(0).push(toPush);
		        toPush = flowBuffer.dequeue();
		    }
		#endif
	    }
    }
    output(0).push_batch(flow);
}

int InsultRemover::removeInsult(const char *insult)
{
    int result = fcb->insultremover.flowBuffer.removeInFlow(fcb, insult);

    // While we keep finding complete insults in the packet
    while(result == 1)
    {
        fcb->insultremover.counterRemoved += 1;
        result = fcb->insultremover.flowBuffer.removeInFlow(fcb, insult);
    }

    return result;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(InsultRemover)
ELEMENT_MT_SAFE(InsultRemover)
