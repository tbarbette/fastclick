#include <click/config.h>
#include "insultremover.hh"
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>

CLICK_DECLS

InsultRemover::InsultRemover()
{

}

int InsultRemover::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

Packet* InsultRemover::processPacket(Packet* p)
{
    WritablePacket *packet = p->uniqueify();
    if(isPacketContentEmpty(packet))
        return packet;

    char *source = (char*)getPacketContent(packet);
    char* firstOccur = NULL;

    while(source != NULL)
    {
        firstOccur = strstr(source, "and");
        if(firstOccur != NULL)
        {
            firstOccur[0] = '*';
            firstOccur[1] = '*';
            firstOccur[2] = '*';

            modifyPacket(p, 0);
        }

        source = firstOccur;
    }

    return packet;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(InsultRemover)
//ELEMENT_MT_SAFE(InsultRemover)
