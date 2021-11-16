#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/tcp.h>
#include <clicknet/ip.h>
#include "simpletcpreorder.hh"

CLICK_DECLS

SimpleTCPReorder::SimpleTCPReorder() : _verbose(false)
{
}

SimpleTCPReorder::~SimpleTCPReorder()
{
}

int
SimpleTCPReorder::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if(Args(conf, this, errh)
    .read_p("VERBOSE",_verbose)
    .complete() < 0)
        return -1;

    return 0;
}


int
SimpleTCPReorder::initialize(ErrorHandler *errh) {

    return 0;
}

void*
SimpleTCPReorder::cast(const char *n) {
   return FlowSpaceElement<fcb_simpletcpreorder>::cast(n);
}

void SimpleTCPReorder::push_flow(int port, fcb_simpletcpreorder* tcpreorder, PacketBatch *batch)
{
    // Ensure that the pointer in the FCB is set
    if (!checkFirstPacket(tcpreorder, batch)) {
    }

    bool had_awaiting = tcpreorder->packetList != 0;

    //Fast path, if no waiting packets and send everything which is in order
    if (likely(!had_awaiting)) {
        int count = 0;
        Packet* last;
        FOR_EACH_PACKET(batch, packet)
        {
            tcp_seq_t currentSeq = getSequenceNumber(packet);
            // We check if the current packet is the expected one (if not, there is a gap)
            if(currentSeq != tcpreorder->expectedPacketSeq)
            {

                if (count != 0) {
                    PacketBatch* second;
                    batch->cut(last, count, second);
                    output_push_batch(0,batch);
                    batch = second;
                }
                goto unordered;
            }

            // Compute the sequence number of the next packet
            tcpreorder->expectedPacketSeq = getNextSequenceNumber(packet);
            tcpreorder->lastSent = currentSeq;
            ++count;
            last = packet;
        }

        //If we are here, everything is in order
        output_push_batch(0,batch);
        //click_chatter("Pushed ordered batch flow %p uc %d",tcpreorder, fcb_stack->count());
        return;

    }
    unordered:
    if (unlikely(_verbose))
        click_chatter("Flow is now unordered... Awaiting %lu, have %lu", tcpreorder->expectedPacketSeq, getSequenceNumber(batch->first()));

    int num = batch->count();
    // Complexity: O(k) (k elements in the batch)
    FOR_EACH_PACKET_SAFE(batch, packet)
    {
        if (!putPacketInList(tcpreorder, packet)) {
            num--;
            continue;
        }
    }


    tcpreorder->packetListLength += num;
    int before = tcpreorder->packetListLength;

    PacketBatch* inorderBatch = sendEligiblePackets(tcpreorder,had_awaiting);

    if (tcpreorder->packetList) {
        assert(tcpreorder->packetListLength);
        //TODO add timer
    }
    if (inorderBatch) {
        //click_chatter("Hole is filled, flushing %d", inorderBatch->count() );
        output_push_batch(0,inorderBatch);
    }
}

/**
 * @pre packetListLength is correct
 * @post it is still correct
 */
PacketBatch* SimpleTCPReorder::sendEligiblePackets(struct fcb_simpletcpreorder *tcpreorder, bool had_awaiting)
{
    Packet* packet = tcpreorder->packetList;
    Packet* last = 0;
    PacketBatch* batch = NULL;
    int count = 0;
    while(packet != NULL)
    {
        tcp_seq_t currentSeq = getSequenceNumber(packet);

        // We check if the current packet is the expected one (if not, there is a gap)
/*        if(currentSeq > tcpreorder->expectedPacketSeq)
        {
            if (_verbose)
                click_chatter("Not the expected packet, have %d expected %d, last sent is %d. Count is %d. Uc %d",currentSeq,tcpreorder->expectedPacketSeq,tcpreorder->lastSent, count,fcb_stack->count());
            //The packet 
//            if (getNextSequenceNumber(
            tcpreorder->packetList = packet;
            // Check before exiting that we did not have a batch to send
            // TODO : Send pro-active retransmit if option
            goto send_batch;
        }
*/
        // Compute the sequence number of the next packet
        tcpreorder->expectedPacketSeq = getNextSequenceNumber(packet);

        // Store the sequence number of the last packet sent
        tcpreorder->lastSent = currentSeq;

        // Send packet
        if(batch == NULL)
            batch = PacketBatch::start_head(packet);
        count++;

        // Free memory and remove node from the list
        last = packet;
        packet = packet->next();
    }
    tcpreorder->packetList = 0;


  send_batch:
  if (!tcpreorder->packetList && had_awaiting) {
      //We don't have awaiting packets anymore, remove the fct
      //click_chatter("We are now in order, removing release fct");
  } else if (tcpreorder->packetList && !had_awaiting) {
      //Set release fnt
      if (_verbose)
          click_chatter("Out of order, setting release fct");
  }
    tcpreorder->packetList = packet;
    tcpreorder->packetListLength -= count;
    // We now send the batch we just built
    if(batch != NULL) {
        assert(count > 0);
        batch->set_count(count);
        batch->set_tail(last);
        last->set_next(0);
        flow_assert(batch->first()->find_count() == count);

        return batch;
    }
    return 0;
}

bool SimpleTCPReorder::putPacketInList(struct fcb_simpletcpreorder* tcpreorder, Packet* packetToAdd)
{
    Packet* last = NULL;
    Packet* packetNode = tcpreorder->packetList;
    auto pSeq = getSequenceNumber(packetToAdd);
    // Browse the list until we find a packet with a greater sequence number than the
    // packet to add in the list
    while(packetNode != NULL
        && (SEQ_LT(getSequenceNumber(packetNode), pSeq)))
    {
        last = packetNode;
        packetNode = packetNode->next();
    }

    if (packetNode && (getSequenceNumber(packetNode) == pSeq)) {
        click_chatter("Identical");
        packetToAdd -> kill();
        return false;
    }

    //SEQ_LT(getAckNumber(packetNode), getAckNumber(packetToAdd)))

    // Check if we need to add the node as the head of the list
    if(last == NULL)
        tcpreorder->packetList = packetToAdd; // If so, the list points to the node to add
    else
        last->set_next(packetToAdd); // If not, the previous node in the list now points to the node to add

     // The node to add points to the first node with a greater sequence number
    packetToAdd->set_next(packetNode);
    return true;
}

void SimpleTCPReorder::killList(struct fcb_simpletcpreorder* tcpreorder) {
        SFCB_STACK( //Packet in the list have no reference
            FOR_EACH_PACKET_LL_SAFE(tcpreorder->packetList,p) {
                click_chatter("WARNING : Non-free SimpleTCPReorder flow bucket");
                p->kill();
            }
        );
        tcpreorder->packetList = 0;
        tcpreorder->packetListLength = 0;
}

bool SimpleTCPReorder::checkFirstPacket(struct fcb_simpletcpreorder* tcpreorder, PacketBatch* batch)
{
    Packet* packet = batch->first();
    const click_tcp *tcph = packet->tcp_header();
    uint8_t flags = tcph->th_flags;

    // Update the expected sequence number
    if (!tcpreorder->expectedPacketSeq)
        tcpreorder->expectedPacketSeq = getNextSequenceNumber(packet);

    return true;
}


CLICK_ENDDECLS
EXPORT_ELEMENT(SimpleTCPReorder)
ELEMENT_MT_SAFE(SimpleTCPReorder)
