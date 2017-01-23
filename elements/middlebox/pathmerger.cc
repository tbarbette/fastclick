#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include "pathmerger.hh"
#include "tcpelement.hh"
/*
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

int PathMerger::getPortForPacket(Packet *packet)
{
    tcp_seq_t seqNumber = TCPElement::getSequenceNumber(packet);
    return fcb->pathmerger.portMap.get(seqNumber);
}

void PathMerger::setPortForPacket(Packet *packet, int port)
{
    tcp_seq_t seqNumber = TCPElement::getSequenceNumber(packet);
    fcb->pathmerger.portMap.set(seqNumber, port);
}

void PathMerger::addStackElementInList(StackElement* element, int port)
{
    previousStackElements[port] = element;
}

StackElement* PathMerger::getElementForPacket(Packet* packet)
{
    StackElement *previousElem = NULL;
    int port = getPortForPacket(packet);

    if(port == 0 || port == 1)
        previousElem = previousStackElements[port];

    return previousElem;
}

void PathMerger::setPacketModified(WritablePacket* packet)
{
    StackElement *previousElem = getElementForPacket(packet);

    if(previousElem != NULL)
        previousElem->setPacketModified(packet);
}

void PathMerger::removeBytes(WritablePacket* packet, uint32_t position, uint32_t length)
{
    StackElement *previousElem = getElementForPacket(packet);

    if(previousElem != NULL)
        previousElem->removeBytes(packet, position, length);
}

void PathMerger::insertBytes(WritablePacket* packet, uint32_t position, uint32_t length)
{
    StackElement *previousElem = getElementForPacket(packet);

    if(previousElem != NULL)
        previousElem->insertBytes(packet, position, length);
}

void PathMerger::requestMoreBytes()
{
    // Only send this message on one of the path
    // The message is intented to be received by TCPIn which is at the
    // beginning of the stack of elements. Thus, it will receive the message
    // no matter on which port we send it. However, if we send it on both
    // ports, it will receive the message twice.
    StackElement *previousElem = previousStackElements[0];

    previousElem->requestMoreBytes(fcb);
}

void PathMerger::packetSent(Packet* packet)
{
    StackElement *previousElem = getElementForPacket(packet);

    if(previousElem != NULL)
        previousElem->packetSent(packet);

    // Remove the entry corresponding to the packet to free memory
    removeEntry(packet);
}

void PathMerger::removeEntry(Packet *packet)
{
    tcp_seq_t seqNumber = TCPElement::getSequenceNumber(packet);
    fcb->pathmerger.portMap.erase(seqNumber);
}

void PathMerger::addEntry(Packet *packet, int port)
{
    tcp_seq_t seqNumber = TCPElement::getSequenceNumber(packet);
    fcb->pathmerger.portMap.set(seqNumber, port);
}

CLICK_ENDDECLS
*/

