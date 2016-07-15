#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include "pathmerger.hh"
#include "tcpelement.hh"

CLICK_DECLS

PathMerger::PathMerger()
{
    previousStackElements[0] = NULL;
    previousStackElements[1] = NULL;
}

PathMerger::~PathMerger()
{

}

int PathMerger::configure(Vector<String> &, ErrorHandler *)
{
    return 0;
}

void PathMerger::push_packet(int port, Packet *packet)
{
    // Similate Middleclick's FCB management
    // We traverse the function stack waiting for TCPIn to give the flow
    // direction.
    unsigned int flowDirection = determineFlowDirection();
    // Add an entry to the hashtable in order to remember from which port
    // this packet came from
    addEntry(&fcbArray[flowDirection], packet, port);

    if(packet != NULL)
        output(0).push(packet);
}

#if HAVE_BATCH
void PathMerger::push_batch(int port, PacketBatch *batch)
{
    // Similate Middleclick's FCB management
    // We traverse the function stack waiting for TCPIn to give the flow
    // direction.
    unsigned int flowDirection = determineFlowDirection();
    // Add an entry to the hashtable in order to remember from which port
    // this packet came from
    FOR_EACH_PACKET(batch, packet)
        addEntry(&fcbArray[flowDirection], packet, port);

    if(batch != NULL)
        output_push_batch(0, batch);
}
#endif


int PathMerger::getPortForPacket(struct fcb* fcb, Packet *packet)
{
    tcp_seq_t seqNumber = getSequenceNumber(packet);
    return fcb->pathmerger.portMap.get(seqNumber);
}

void PathMerger::setPortForPacket(struct fcb *fcb, Packet *packet, int port)
{
    tcp_seq_t seqNumber = getSequenceNumber(packet);
    fcb->pathmerger.portMap.set(seqNumber, port);
}

void PathMerger::addStackElementInList(StackElement* element, int port)
{
    previousStackElements[port] = element;
}

StackElement* PathMerger::getElementForPacket(struct fcb* fcb, Packet* packet)
{
    StackElement *previousElem = NULL;
    int port = getPortForPacket(fcb, packet);

    if(port == 0 || port == 1)
        previousElem = previousStackElements[port];

    return previousElem;
}

void PathMerger::setPacketDirty(struct fcb *fcb, WritablePacket* packet)
{
    StackElement *previousElem = getElementForPacket(fcb, packet);

    if(previousElem != NULL)
        previousElem->setPacketDirty(fcb, packet);
}

bool PathMerger::isLastUsefulPacket(struct fcb* fcb, Packet *packet)
{
    StackElement *previousElem = getElementForPacket(fcb, packet);

    if(previousElem != NULL)
        return previousElem->isLastUsefulPacket(fcb, packet);
    else
        return false;
}

void PathMerger::removeBytes(struct fcb *fcb, WritablePacket* packet, uint32_t position, uint32_t length)
{
    StackElement *previousElem = getElementForPacket(fcb, packet);

    if(previousElem != NULL)
        previousElem->removeBytes(fcb, packet, position, length);
}

WritablePacket* PathMerger::insertBytes(struct fcb *fcb, WritablePacket* packet, uint32_t position, uint32_t length)
{
    StackElement *previousElem = getElementForPacket(fcb, packet);

    if(previousElem != NULL)
        return previousElem->insertBytes(fcb, packet, position, length);
    else
        return NULL;
}

void PathMerger::requestMorePackets(struct fcb *fcb, Packet *packet, bool force)
{
    StackElement *previousElem = getElementForPacket(fcb, packet);

    if(previousElem != NULL)
        previousElem->requestMorePackets(fcb, packet, force);
}

void PathMerger::packetSent(struct fcb *fcb, Packet* packet)
{
    StackElement *previousElem = getElementForPacket(fcb, packet);

    if(previousElem != NULL)
        previousElem->packetSent(fcb, packet);

    // Remove the entry corresponding to the packet to free memory
    removeEntry(fcb, packet);
}

unsigned int PathMerger::determineFlowDirection()
{
    // We could chose either of the inputs as the answer would be the same
    return previousStackElements[0]->determineFlowDirection();
}

void PathMerger::closeConnection(struct fcb *fcb, WritablePacket* packet, bool graceful, bool bothSides)
{
    StackElement *previousElem = getElementForPacket(fcb, packet);

    if(previousElem != NULL)
        previousElem->closeConnection(fcb, packet, graceful, bothSides);
}

void PathMerger::removeEntry(struct fcb *fcb, Packet *packet)
{
    tcp_seq_t seqNumber = getSequenceNumber(packet);
    fcb->pathmerger.portMap.erase(seqNumber);
}

void PathMerger::addEntry(struct fcb *fcb, Packet *packet, int port)
{
    tcp_seq_t seqNumber = getSequenceNumber(packet);
    fcb->pathmerger.portMap.set(seqNumber, port);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(PathMerger)
ELEMENT_REQUIRES(TCPElement)
ELEMENT_MT_SAFE(PathMerger)
