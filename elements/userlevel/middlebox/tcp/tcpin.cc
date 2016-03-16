#include <click/config.h>
#include "tcpin.hh"
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/tcp.h>

CLICK_DECLS

TCPIn::TCPIn() : outElement(NULL), returnElement(NULL)
{

}

int TCPIn::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String returnName = "";
    String outName = "";

    if(Args(conf, this, errh)
    .read_p("OUTNAME", outName)
    .read_p("RETURNNAME", returnName)
    .complete() < 0)
        return -1;

    if(outName == "" || returnName == "")
    {
        click_chatter("Missing parameter(s): TCPIn requires two parameters (OUTNAME and RETURNNAME)");
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
    else
    {
        this->returnElement = (TCPIn*)returnElement;
        this->outElement = (TCPOut*)outElement;
    }

    return 0;
}

Packet* TCPIn::processPacket(Packet* p)
{
    WritablePacket *packet = p->uniqueify();

    const click_tcp *tcph = p->tcp_header();

    // Compute the offset of the TCP payload
    unsigned tcph_len = tcph->th_off << 2;
    uint16_t offset = (uint16_t)(p->transport_header() + tcph_len - p->data());
    setContentOffset(p, offset);

    // Update the ack number according to the bytestreammaintainer of the other path
    tcp_seq_t ackNumber = getAckNumber(packet);
    tcp_seq_t offsetAck = getReturnElement()->getOutElement()->getByteStreamMaintainer()->getAckOffset(ackNumber);

    if(offsetAck != 0)
    {
        click_chatter("Ack number %u becomes %u (%u)", ackNumber, ackNumber + offsetAck, offsetAck);
        setAckNumber(packet, ackNumber + offsetAck);
        modifyPacket(p);
    }
    else
    {
        click_chatter("Ack number %u stays the same", ackNumber);
    }

    return packet;
}

void TCPIn::packetModified(Packet* p)
{
    // Annotate the packet to indicate it has been modified
    // While going through "out elements", the checksum will be recomputed
    setAnnotationModification(p, true);
}

TCPOut* TCPIn::getOutElement()
{
    return outElement;
}

TCPIn* TCPIn::getReturnElement()
{
    return returnElement;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(TCPIn)
//ELEMENT_MT_SAFE(TCPIn)
