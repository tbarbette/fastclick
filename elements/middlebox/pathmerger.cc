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

void PathMerger::push(int port, Packet *packet)
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


int PathMerger::getPortForPacket(struct fcb* fcb, Packet *packet)
{
    tcp_seq_t seqNumber = TCPElement::getSequenceNumber(packet);
    return fcb->pathmerger.portMap.get(seqNumber);
}

void PathMerger::setPortForPacket(struct fcb *fcb, Packet *packet, int port)
{
    tcp_seq_t seqNumber = TCPElement::getSequenceNumber(packet);
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

void PathMerger::setPacketModified(struct fcb *fcb, WritablePacket* packet)
{
    StackElement *previousElem = getElementForPacket(fcb, packet);

    if(previousElem != NULL)
        previousElem->setPacketModified(fcb, packet);
}

void PathMerger::removeBytes(struct fcb *fcb, WritablePacket* packet, uint32_t position, uint32_t length)
{
    StackElement *previousElem = getElementForPacket(fcb, packet);

    if(previousElem != NULL)
        previousElem->removeBytes(fcb, packet, position, length);
}

void PathMerger::insertBytes(struct fcb *fcb, WritablePacket* packet, uint32_t position, uint32_t length)
{
    StackElement *previousElem = getElementForPacket(fcb, packet);

    if(previousElem != NULL)
        previousElem->insertBytes(fcb, packet, position, length);
}

void PathMerger::requestMoreBytes(struct fcb *fcb)
{
    // Only send this message on one of the path
    // The message is intented to be received by TCPIn which is at the
    // beginning of the stack of elements. Thus, it will receive the message
    // no matter on which port we send it. However, if we send it on both
    // ports, it will receive the message twice.
    StackElement *previousElem = previousStackElements[0];

    previousElem->requestMoreBytes(fcb);
}

void PathMerger::packetSent(struct fcb *fcb, Packet* packet)
{
    StackElement *previousElem = getElementForPacket(fcb, packet);

    if(previousElem != NULL)
        previousElem->packetSent(fcb, packet);

    // Remove the entry corresponding to the packet to free memory
    removeEntry(fcb, packet);
}

void PathMerger::removeEntry(struct fcb *fcb, Packet *packet)
{
    tcp_seq_t seqNumber = TCPElement::getSequenceNumber(packet);
    fcb->pathmerger.portMap.erase(seqNumber);
}

void PathMerger::addEntry(struct fcb *fcb, Packet *packet, int port)
{
    tcp_seq_t seqNumber = TCPElement::getSequenceNumber(packet);
    fcb->pathmerger.portMap.set(seqNumber, port);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(PathMerger)
//ELEMENT_MT_SAFE(PathMerger)
