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

    if(nbPackets < 15)
        requestMorePackets(fcb, packet);

/*
    if(nbPackets == 10)
        closeConnection(fcb, packet, false, true);
        */



    insertBytes(fcb, packet, 0, 6);

    source[0] = 'H';
    source[1] = 'E';
    source[2] = 'L';
    source[3] = 'L';
    source[4] = 'O';
    source[5] = ' ';

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


    return packet;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(InsultRemover)
//ELEMENT_MT_SAFE(InsultRemover)
