#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include "httpin.hh"

CLICK_DECLS

HTTPIn::HTTPIn()
{
}

int HTTPIn::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

Packet* HTTPIn::processPacket(struct fcb *fcb, Packet* p)
{
    WritablePacket *packet = p->uniqueify();

    if(!fcb->httpin.headerFound)
    {
        removeHeader(fcb, packet, "Accept-Encoding");
    }

    // Compute the offset of the HTML payload
    const char* source = strstr((const char*)getPacketContentConst(packet), "\r\n\r\n");
    if(source != NULL)
    {
        uint32_t offset = (int)(source - (char*)packet->data() + 4);
        setContentOffset(packet, offset);
        fcb->httpin.headerFound = true;
    }

    return packet;
}

void HTTPIn::removeHeader(struct fcb *fcb, WritablePacket* packet, const char* header)
{
    unsigned char* source = getPacketContent(packet);
    unsigned char* beginning = (unsigned char*)strstr((char*)source, header);

    if(beginning == NULL)
        return;

    unsigned char* end = (unsigned char*)strstr((char*)beginning, "\r\n");
    if(end == NULL)
        return;
    unsigned nbBytesToRemove = (end - beginning) + strlen("\r\n");

    uint32_t position = beginning - source;

    removeBytes(fcb, packet, position, nbBytesToRemove);

    click_chatter("Removed header: %s", header);
    setPacketDirty(fcb, packet);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(HTTPIn)
//ELEMENT_MT_SAFE(HTTPIn)
