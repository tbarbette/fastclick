#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include "insultremover.hh"
#include "tcpelement.hh"

CLICK_DECLS

InsultRemover::InsultRemover()
{

}

int InsultRemover::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

Packet* InsultRemover::processPacket(struct fcb *fcb, Packet* p)
{
    WritablePacket *packet = p->uniqueify();
    if(isPacketContentEmpty(packet))
        return packet;

    unsigned char *source = getPacketContent(packet);
    uint32_t contentOffset = getContentOffset(packet);
    unsigned char* firstOccur = NULL;

    static int nbPackets = 0;

    nbPackets++;

    /*if(nbPackets == 5)
        closeConnection(fcb, packet, true);*/

    while(source != NULL)
    {
        firstOccur = (unsigned char*)strstr((char*)source, "and");
        if(firstOccur != NULL)
        {
            uint32_t position = firstOccur - packet->data();
            removeBytes(fcb, packet, position, 3);
            setPacketDirty(fcb, packet);
        }
            source = firstOccur;
    }

    return packet;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(InsultRemover)
//ELEMENT_MT_SAFE(InsultRemover)
