#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include "insultremover.hh"
#include "tcpelement.hh"

CLICK_DECLS

InsultRemover::InsultRemover() : poolBufferEntries(POOL_BUFFER_ENTRIES_SIZE)
{
    #if HAVE_BATCH
        in_batch_mode = BATCH_MODE_YES;
    #endif
}

int InsultRemover::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

Packet* InsultRemover::processPacket(struct fcb *fcb, Packet* p)
{
    WritablePacket *packet = p->uniqueify();
    assert(packet != NULL);

    if(!fcb->insultremover.flowBuffer.isInitialized())
        fcb->insultremover.flowBuffer.initialize(this, &poolBufferEntries);

    if(isPacketContentEmpty(packet))
        return packet;

    FlowBuffer &flowBuffer = fcb->insultremover.flowBuffer;
    flowBuffer.enqueue(packet);

    bool needMorePackets = false;

    int result = removeInsult(fcb, "and");
    if(result == 0)
        needMorePackets = true;

    result = removeInsult(fcb, "astronomical");
    if(result == 0)
        needMorePackets = true;

    // If the beginning of an insult could be found at the end of a packet
    // we keep it in the buffer. Otherwise, we flush the buffer as we know
    // that we will not be able to find a part of an insult in it
    // Of course, if we have the last packet of the flow, we know
    // that it is useless to buffer it until the next one.
    if(!isLastUsefulPacket(fcb, packet) && needMorePackets)
        requestMorePackets(fcb, packet);
    else
    {
        click_chatter("Flushing");
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

    return NULL;

    /*

    unsigned char *source = getPacketContent(packet);
    uint32_t contentOffset = getContentOffset(packet);
    unsigned char* firstOccur = NULL;

    static int nbPackets = 0;

    nbPackets++;

    if(nbPackets < 15)
        requestMorePackets(fcb, packet);
    */
    /*
    if(nbPackets == 10)
        closeConnection(fcb, packet, false, true);
    */
    /*
    packet = insertBytes(fcb, packet, 0, 6);

    source[0] = 'H';
    source[1] = 'E';
    source[2] = 'L';
    source[3] = 'L';
    source[4] = 'O';
    source[5] = ' ';

    source = getPacketContent(packet);
    firstOccur = source;
    while(firstOccur != NULL)
    {
        firstOccur = (unsigned char*)strstr((char*)firstOccur, "and");
        if(firstOccur != NULL)
        {
            uint32_t position = firstOccur - source;
            removeBytes(fcb, packet, position, 3);
            setPacketDirty(fcb, packet);
        }
    }
    */
}

int InsultRemover::removeInsult(struct fcb* fcb, const char *insult)
{
    int result = fcb->insultremover.flowBuffer.removeInFlow(fcb, insult);

    // While we keep finding entire insults in the packet
    while(result == 1)
    {
        fcb->insultremover.counterRemoved += 1;
        result = fcb->insultremover.flowBuffer.removeInFlow(fcb, insult);
    }

    return result;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(InsultRemover)
//ELEMENT_MT_SAFE(InsultRemover)
