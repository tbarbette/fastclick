#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include "insultremover.hh"

CLICK_DECLS

InsultRemover::InsultRemover()
{

}

int InsultRemover::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

void InsultRemover::push_batch(int port, PacketBatch* flow)
{
    FOR_EACH_PACKET_SAFE(flow, p) {
        WritablePacket *packet = p->uniqueify();
        if (isPacketContentEmpty(packet))
            continue;

        unsigned char *source = getPacketContent(packet);
        uint32_t contentOffset = getContentOffset(packet);
        unsigned char *firstOccur = NULL;

        while (source != NULL) {
            firstOccur = (unsigned char *) strstr((char *) source, "and");
            if (firstOccur != NULL) {
                uint32_t position = firstOccur - packet->data();

                removeBytes(packet, position, 3);
                setPacketModified(packet);
            }

            source = firstOccur;
        }
    }
    output(0).push_batch(flow);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(InsultRemover)
//ELEMENT_MT_SAFE(InsultRemover)
