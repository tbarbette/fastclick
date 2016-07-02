#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/tcp.h>
#include "tcpin.hh"
#include "ipelement.hh"

CLICK_DECLS

TCPIn::TCPIn() : outElement(NULL), returnElement(NULL),
    poolModificationNodes(MODIFICATIONNODES_POOL_SIZE),
    poolModificationLists(MODIFICATIONLISTS_POOL_SIZE),
    poolFcbTcpCommon(TCPCOMMON_POOL_SIZE),
    tableFcbTcpCommon(NULL)
{
}

int TCPIn::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String returnName = "";
    String outName = "";
    int flowDirectionParam = -1;

    if(Args(conf, this, errh)
    .read_p("FLOWDIRECTION", flowDirectionParam)
    .read_p("OUTNAME", outName)
    .read_p("RETURNNAME", returnName)
    .complete() < 0)
        return -1;

    if(flowDirectionParam == -1 || outName == "" || returnName == "")
    {
        click_chatter("Missing parameter(s): TCPIn requires three parameters (FLOWDIRECTION, OUTNAME and RETURNNAME)");
        return -1;
    }

    Element* returnElement = this->router()->find(returnName, errh);
    Element* outElement = this->router()->find(outName, errh);

    if(returnElement == NULL)
    {
        click_chatter("Error: Could not find TCPIn element "
            "called \"%s\".", returnName.c_str());
        return -1;
    }
    else if(outElement == NULL)
    {
        click_chatter("Error: Could not find TCPOut element "
            "called \"%s\".", outName.c_str());
        return -1;
    }
    else if(strcmp("TCPIn", returnElement->class_name()) != 0)
    {
        click_chatter("Error: Element \"%s\" is not a TCPIn element "
            "but a %s element.", returnName.c_str(), returnElement->class_name());
        return -1;
    }
    else if(strcmp("TCPOut", outElement->class_name()) != 0)
    {
        click_chatter("Error: Element \"%s\" is not a TCPOut element "
            "but a %s element.", outName.c_str(), outElement->class_name());
        return -1;
    }
    else if(flowDirectionParam != 0 && flowDirectionParam != 1)
    {
        click_chatter("Error: FLOWDIRECTION %u is not valid.", flowDirectionParam);
        return -1;
    }
    else
    {
        this->returnElement = (TCPIn*)returnElement;
        this->outElement = (TCPOut*)outElement;
        this->outElement->setInElement(this);
        setFlowDirection((unsigned int)flowDirectionParam);
        this->outElement->setFlowDirection(getFlowDirection());
    }

    return 0;
}

Packet* TCPIn::processPacket(struct fcb *fcb, Packet* p)
{
    // Ensure that the pointers in the FCB are set
    if(fcb->tcpin.poolModificationNodes == NULL)
        fcb->tcpin.poolModificationNodes = &poolModificationNodes;

    if(fcb->tcpin.poolModificationLists == NULL)
        fcb->tcpin.poolModificationLists = &poolModificationLists;

    // Assign the tcp_common structure if not already done
    if(fcb->tcp_common == NULL)
    {
        if(!assignTCPCommon(fcb, p))
        {
            // The allocation failed, meaning that the packet is not a SYN
            // packet. This is not supposed to happen and it means that
            // the first two packets of the connection are not SYN packets
            click_chatter("Warning: Trying to assign a common tcp memory area"
                " for a non-SYN packet");
            p->kill();

            return NULL;
        }
    }
    else
    {
        // TODO we could check here that the packet is not a SYN packet
    }

    WritablePacket *packet = p->uniqueify();

    const click_tcp *tcph = packet->tcp_header();

    // Compute the offset of the TCP payload
    unsigned tcph_len = tcph->th_off << 2;
    uint16_t offset = (uint16_t)(packet->transport_header() + tcph_len - packet->data());
    setContentOffset(packet, offset);
    fcb->tcpin.tcpOffset = offset;

    tcp_seq_t ackNumber = getAckNumber(packet);

    // Update the ack number according to the bytestreammaintainer of the other direction
    tcp_seq_t newAckNumber = fcb->tcp_common->maintainers[getOppositeFlowDirection()].mapAck(ackNumber);

    // Check if the current packet is just an ACK without more information
    if(isJustAnAck(packet))
    {
        // If it is the case, check that the ACK value is greater than what
        // we have already sent earlier
        if(newAckNumber < fcb->tcp_common->maintainers[getFlowDirection()].getLastAck())
        {
            // If this is not the case, the packet does not give any information
            // We can drop it
            click_chatter("Received an ACK for a sequence number already ACKed. Dropping it.");
            packet->kill();
            return NULL;
        }
    }

    // Prune the ByteSreamMaintainer
    // The value to send must be the ACK value BEFORE mapping
    fcb->tcp_common->maintainers[getOppositeFlowDirection()].ackReceived(ackNumber);
    // Prune the RetransmissionManager
    // TODO indicate to RetransmissionManager (opposite) that we received an ACK so it can prune its tree

    if(ackNumber != newAckNumber)
    {
        click_chatter("Ack number %u becomes %u in flow %u", ackNumber, newAckNumber, flowDirection);
        setAckNumber(packet, newAckNumber);
        setPacketModified(fcb, packet);
    }
    else
    {
        click_chatter("Ack number %u stays the same in flow %u", ackNumber, flowDirection);
    }

    return packet;
}

TCPOut* TCPIn::getOutElement()
{
    return outElement;
}

TCPIn* TCPIn::getReturnElement()
{
    return returnElement;
}

void TCPIn::closeConnection(struct fcb* fcb, uint32_t saddr, uint32_t daddr, uint16_t sport,
                             uint16_t dport, tcp_seq_t seq, tcp_seq_t ack, bool graceful)
{
    closeConnection(fcb, saddr, daddr, sport, dport, seq, ack, graceful, true);
}

void TCPIn::closeConnection(struct fcb* fcb, uint32_t saddr, uint32_t daddr, uint16_t sport,
                             uint16_t dport, tcp_seq_t seq, tcp_seq_t ack, bool graceful, bool initiator)
{
    if(graceful)
    {
        Packet *packet = forgePacket(saddr, daddr, sport, dport, seq, ack, 32120, TH_FIN);
        outElement->push(0, packet);
        fcb->tcpin.closingState = TCPClosingState::FIN_WAIT;

        click_chatter("Sending FIN on flow %u", getFlowDirection());
    }
    else
    {
        Packet *packet = forgePacket(saddr, daddr, sport, dport, seq, ack, 32120, TH_RST);
        outElement->push(0, packet);
        fcb->tcpin.closingState = TCPClosingState::CLOSED;

        click_chatter("Sending RST on flow %u", getFlowDirection());
    }
    // "initiator" indicates which side of the connection closes it (us (true) or the other side (false))
    // If we are the initiator, tell the other side of the connection to do so as well
    if(initiator)
        returnElement->closeConnection(fcb, daddr, saddr, dport, sport, ack, seq, graceful, false);
}

ModificationList* TCPIn::getModificationList(struct fcb *fcb, WritablePacket* packet)
{
    HashTable<tcp_seq_t, ModificationList*> modificationLists = fcb->tcpin.modificationLists;

    ModificationList* list = fcb->tcpin.modificationLists.get(getSequenceNumber(packet));

    // If no list was assigned to this packet, create a new one
    if(list == NULL)
    {
        ModificationList* listPtr = fcb->tcpin.poolModificationLists->getMemory();
        // Call the constructor manually to have a clean object
        list = new(listPtr) ModificationList(&poolModificationNodes);
        fcb->tcpin.modificationLists.set(getSequenceNumber(packet), list);
    }

    return list;
}

bool TCPIn::hasModificationList(struct fcb *fcb, Packet* packet)
{
    HashTable<tcp_seq_t, ModificationList*> modificationLists = fcb->tcpin.modificationLists;

    ModificationList* list = fcb->tcpin.modificationLists.get(getSequenceNumber(packet));

    return (list != NULL);
}

void TCPIn::removeBytes(struct fcb *fcb, WritablePacket* packet, uint32_t position, uint32_t length)
{
    //click_chatter("Removing %u bytes", length);
    ModificationList* list = getModificationList(fcb, packet);

    tcp_seq_t seqNumber = getSequenceNumber(packet);

    uint32_t tcpOffset = fcb->tcpin.tcpOffset;
    list->addModification(seqNumber + position - tcpOffset, -((int)length));

    unsigned char *source = packet->data();
    uint32_t bytesAfter = packet->length() - position;

    memmove(&source[position], &source[position + length], bytesAfter);
    packet->take(length);

    // Continue in the stack function
    StackElement::removeBytes(fcb, packet, position, length);
}

WritablePacket* TCPIn::insertBytes(struct fcb *fcb, WritablePacket* packet, uint32_t position, uint32_t length)
{
    click_chatter("Adding %u bytes", length);

    tcp_seq_t seqNumber = getSequenceNumber(packet);

    uint32_t tcpOffset = fcb->tcpin.tcpOffset;
    getModificationList(fcb, packet)->addModification(seqNumber + position - tcpOffset, (int)length);

    uint32_t bytesAfter = packet->length() - position;

    WritablePacket *newPacket = packet->put(length);
    assert(newPacket != NULL);
    unsigned char *source = newPacket->data();

    memmove(&source[position + length], &source[position], bytesAfter);

    return newPacket;
}

void TCPIn::requestMorePackets(struct fcb *fcb, Packet *packet)
{
    ackPacket(fcb, packet, true);

    // Continue in the stack function
    StackElement::requestMorePackets(fcb, packet);
}

void TCPIn::ackPacket(struct fcb *fcb, Packet* packet, bool ackMapped)
{
    // Get the information needed to ack the given packet
    uint32_t saddr = IPElement::getDestinationAddress(packet);
    uint32_t daddr = IPElement::getSourceAddress(packet);
    uint16_t sport = getDestinationPort(packet);
    uint16_t dport = getSourcePort(packet);
    // The SEQ value is the initial ACK value in the packet sent
    // by the source.
    tcp_seq_t seq = getAckNumber(packet);
    // If the ACK has been mapped, we map it back to get its initial value
    if(ackMapped)
        seq = fcb->tcp_common->maintainers[getOppositeFlowDirection()].mapSeq(seq);
    uint16_t winSize = getWindowSize(packet);
    // The ACK is the sequence number sent by the source
    // to which we add the size of the payload to acknowledge it
    tcp_seq_t ack = getSequenceNumber(packet) + getPayloadLength(packet);

    if(isFinOrSyn(packet))
        ack++;

    // Craft and send the ack
    outElement->sendAck(saddr, daddr, sport, dport, seq, ack, winSize);

    // Update the number of the last ack sent for the other side
    fcb->tcp_common->maintainers[getOppositeFlowDirection()].setLastAck(ack);
}

void TCPIn::setPacketModified(struct fcb *fcb, WritablePacket* packet)
{
    // Annotate the packet to indicate it has been modified
    // While going through "out elements", the checksum will be recomputed
    setAnnotationModification(packet, true);

    // Continue in the stack function
    StackElement::setPacketModified(fcb, packet);
}

unsigned int TCPIn::determineFlowDirection()
{
    return getFlowDirection();
}

Packet* TCPIn::checkClosingConnection(struct fcb *fcb, Packet* packet)
{
    // If the connection is not being closed, we simply return the packet
    if(fcb->tcpin.closingState == TCPClosingState::OPEN)
        return packet;

    // If the connection is closed, discard any incoming packet
    if(fcb->tcpin.closingState == TCPClosingState::CLOSED)
    {
        packet->kill();
        return NULL;
    }

    // We have sent a FIN to the source
    if(fcb->tcpin.closingState == TCPClosingState::FIN_WAIT)
    {
        packet->kill();
    }

    if(isFin(packet))
    {
        fcb->tcpin.closingState = TCPClosingState::CLOSED;
        //sendAck(packet);
    }
}

bool TCPIn::assignTCPCommon(struct fcb *fcb, Packet *packet)
{
    const click_tcp *tcph = packet->tcp_header();
    uint8_t flags = tcph->th_flags;
    const click_ip *iph = packet->ip_header();

    // Check if we are in the first two steps of the three-way handshake
    // (SYN packet)
    if(!(flags & TH_SYN))
        return false;

    // Check if we are the side initiating the connection or not
    // (if ACK flag, we are not the initiator)
    if(flags & TH_ACK)
    {
        // Getting the flow ID for the opposite side of the connection
        IPFlowID flowID(iph->ip_dst, tcph->th_dport, iph->ip_src, tcph->th_sport);

        // Get the struct allocated by the initiator
        fcb->tcp_common = returnElement->getTCPCommon(flowID);
    }
    else
    {
        IPFlowID flowID(iph->ip_src, tcph->th_sport, iph->ip_dst, tcph->th_dport);
        // We are the initiator, we need to allocate memory
        struct fcb_tcp_common *allocated = poolFcbTcpCommon.getMemory();
        // Call the constructor with some parameters that will be used
        // to free the memory of the structure when not needed anymore
        allocated = new(allocated) struct fcb_tcp_common(flowID, &tableFcbTcpCommon, &poolFcbTcpCommon);

        // Add an entry if the hashtable
        tableFcbTcpCommon.set(flowID, allocated);

        // Set the pointer in the structure
        fcb->tcp_common = allocated;
    }

    return true;
}


struct fcb_tcp_common* TCPIn::getTCPCommon(IPFlowID flowID)
{
    return tableFcbTcpCommon.get(flowID);
}



TCPIn::~TCPIn()
{

}

CLICK_ENDDECLS
EXPORT_ELEMENT(TCPIn)
//ELEMENT_MT_SAFE(TCPIn)
