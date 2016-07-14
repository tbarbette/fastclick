#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/tcp.h>
#include "tcpmarkmss.hh"

CLICK_DECLS

TCPMarkMSS::TCPMarkMSS()
{
    annotation = 0;
    offset = 0;
    flowDirection = 0;
}

int TCPMarkMSS::configure(Vector<String> &conf, ErrorHandler *errh)
{
    int annotationParam = -1;
    int offsetParam = 0;
    int flowDirectionParam = -1;

    if(Args(conf, this, errh)
    .read_mp("FLOWDIRECTION", flowDirectionParam)
    .read_mp("ANNOTATION",  AnnoArg(2), annotationParam)
    .read("OFFSET", offsetParam)
    .complete() < 0)
        return -1;

    annotation = (int8_t)annotationParam;
    offset = (int16_t)offsetParam;
    flowDirection = (unsigned int)flowDirectionParam;

    return 0;
}

void TCPMarkMSS::push_packet(int, Packet *packet)
{
    Packet* p = markMSS(&fcbArray[flowDirection], packet);

    if(p != NULL)
        output(0).push(p);
}

Packet* TCPMarkMSS::pull(int)
{
    Packet *packet = input(0).pull();

    if(packet == NULL)
        return NULL;

    Packet* p = markMSS(&fcbArray[flowDirection], packet);

    return p;
}

#if HAVE_BATCH
void TCPMarkMSS::push_batch(int, PacketBatch *batch)
{
    EXECUTE_FOR_EACH_PACKET_FCB(markMSS, &fcbArray[flowDirection], batch)

    if(batch != NULL)
        output_push_batch(0, batch);
}

PacketBatch* TCPMarkMSS::pull_batch(int port, int max)
{
    PacketBatch *batch = input_pull_batch(port, max);

    EXECUTE_FOR_EACH_PACKET_FCB(markMSS, &fcbArray[flowDirection], batch)

	return batch;
}
#endif

Packet* TCPMarkMSS::markMSS(struct fcb *fcb, Packet *packet)
{
    if(!isSyn(packet))
    {
        packet->set_anno_u16(annotation, fcb->tcpmarkmss.mss);

        return packet;
    }

    const click_tcp *tcph = packet->tcp_header();

    const uint8_t *optStart = (const uint8_t *) (tcph + 1);
    const uint8_t *optEnd = (const uint8_t *) tcph + (tcph->th_off << 2);

    if(optEnd > packet->end_data())
        optEnd = packet->end_data();

    uint16_t mss = DEFAULT_MSS;

    while(optStart < optEnd)
    {
        if(optStart[0] == TCPOPT_EOL) // End of list
            break; // Stop searching
        else if(optStart[0] == TCPOPT_NOP)
            optStart += 1; // Move to the next option
        else if(optStart[1] < 2 || optStart[1] + optStart > optEnd)
            break; // Avoid malformed options
        else if(optStart[0] == TCPOPT_MAXSEG && optStart[1] == TCPOLEN_MAXSEG)
        {

            fcb->tcpmarkmss.mss = (optStart[2] << 8) | optStart[3];

            break;
        }
        else
            optStart += optStart[1]; // Move to the next option
    }

    if(offset > 0 || offset <= mss)
        fcb->tcpmarkmss.mss += offset;

    packet->set_anno_u16(annotation, fcb->tcpmarkmss.mss);

    return packet;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(TCPMarkMSS)
ELEMENT_REQUIRES(TCPElement)
ELEMENT_MT_SAFE(TCPMarkMSS)
