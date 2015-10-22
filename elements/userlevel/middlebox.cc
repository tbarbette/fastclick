#include <click/config.h>
#include "middlebox.hh"
#include <click/router.hh>
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include <click/args.hh>
#include <click/error.hh>

CLICK_DECLS

Middlebox::Middlebox() : linkedMiddlebox(NULL)
{
    header_processed = false;
}

int
Middlebox::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String linkedElementName = "";

    if(Args(conf, this, errh)
    .read_p("LINKEDELEMENT", linkedElementName)
    .complete() < 0)
        return -1;

    if(linkedElementName != "")
    {
        Element* other = this->router()->find(linkedElementName, errh);
        if(other == NULL)
        {
            click_chatter("Error: Could not find linked Middlebox element "
                "called \"%s\".", linkedElementName.c_str());
            return -1;
        }
        else if(strcmp(this->class_name(), other->class_name()) != 0)
        {
            click_chatter("Error: Linked element \"%s\" is not a Middlebox element "
                "but a %s element.", linkedElementName.c_str(), other->class_name());
            return -1;
        }
        else
        {
            linkedMiddlebox = (Middlebox*)other;
            linkedMiddlebox->helloWorld();
        }
    }
    else
    {
        flow_maintainer = (struct flow_maintainer*)malloc(sizeof(struct flow_maintainer));
        flow_maintainer->seq_offset_other = 0;
        flow_maintainer->packets_seen = 0;
    }

    return 0;
}

void
Middlebox::push(int, Packet *packet)
{
    Packet *p = processPacket(packet);
    output(0).push(p);
}

Packet *
Middlebox::pull(int)
{
    Packet *packet = input(0).pull();

    click_chatter("Pulled packet");
    Packet* p = processPacket(packet);

    return p;
}

Packet*
Middlebox::processPacket(Packet* packet_in)
{
    WritablePacket *packet = packet_in->uniqueify();
    click_ip *iph = packet->ip_header();
    click_tcp *tcph = packet->tcp_header();

    increasePacketsSeen();
    click_chatter("Packet length: %u", packet_in->length());

    // Check if the packet is a TCP packet
    if(!packet->has_network_header() || iph->ip_p != IP_PROTO_TCP)
        return packet;

    // Determine payload
    unsigned tcph_len = tcph->th_off << 2;
    unsigned char *tcp_payload = packet->transport_header() + tcph_len;
    uint32_t payload_length = packet->length() - (packet->transport_header_offset() + tcph_len);

    updateSeqNumbers(packet, !isLinked());

    // Process payload
	if(payload_length > 0)
    	processContent(packet, tcp_payload, payload_length);

    // Recompute checksum
    setChecksum(packet);

    return packet;
}

void
Middlebox::processContent(WritablePacket* packet, unsigned char* content, uint32_t len)
{
    char *source;
    if(strstr((char*)content, "200 OK") != NULL)
    {
        click_chatter("Web page detected");
        source = strstr((char*)content, "\r\n\r\n");
        header_processed = true;
    }
    else
        if(header_processed)
            source = (char*)content;
        else
            return;

    uint32_t source_length = len - (source - (char*)content);
    click_chatter("Source length: %u, payload length: %u", source_length, len);

    for(uint32_t i = 0; i < source_length; ++i)
    {
        char *c = (char *)&(source[i]);

		if(*c == 'a' || *c == 'A')
		{
			memmove(&source[i], &source[i + 1], source_length - i);
			source_length--;
			updatePayloadSize(packet, -1);
			packet->take(1);
			getFlowMaintainer()->seq_offset_other = getFlowMaintainer()->seq_offset_other + 1;
		}
    }

}

void Middlebox::setChecksum(WritablePacket* packet)
{
    click_ip *iph = packet->ip_header();
    click_tcp *tcph = packet->tcp_header();

    unsigned plen = ntohs(iph->ip_len) - (iph->ip_hl << 2);
    tcph->th_sum = 0;
    unsigned csum = click_in_cksum((unsigned char *)tcph, plen);
    tcph->th_sum = click_in_cksum_pseudohdr(csum, iph, plen);
	unsigned hlen = iph->ip_hl << 2;
	iph->ip_sum = 0;
	iph->ip_sum = click_in_cksum((const unsigned char *)iph, hlen);
}

bool Middlebox::isLinked()
{
    return (linkedMiddlebox != NULL);
}

void Middlebox::helloWorld()
{
    click_chatter("Hello, world!");
}

struct flow_maintainer* Middlebox::getFlowMaintainer()
{
    if(isLinked())
        return linkedMiddlebox->getFlowMaintainer();
    else
        return flow_maintainer;
}

void Middlebox::increasePacketsSeen()
{
    getFlowMaintainer()->packets_seen = getFlowMaintainer()->packets_seen + 1;
    click_chatter("Packets seen: %d", getFlowMaintainer()->packets_seen);
}

void Middlebox::updateSeqNumbers(WritablePacket *packet, bool from_initiator)
{
    click_tcp *tcph = packet->tcp_header();
    unsigned int offsetSeq;
    unsigned int offsetAck;

    if(from_initiator)
    {
        offsetSeq = getFlowMaintainer()->seq_offset_initiator;
        offsetAck = getFlowMaintainer()->seq_offset_other;
    }
    else
    {
        offsetSeq = getFlowMaintainer()->seq_offset_other;
        offsetAck = getFlowMaintainer()->seq_offset_initiator;
    }

    tcph->th_seq = htonl(ntohl(tcph->th_seq) - offsetSeq);
	tcph->th_ack = htonl(ntohl(tcph->th_ack) + offsetAck);
}

void Middlebox::updatePayloadSize(WritablePacket* packet, uint32_t offset)
{
    click_ip *iph = packet->ip_header();

    unsigned plen = ntohs(iph->ip_len);

    plen += offset;

    iph->ip_len = htons(plen);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(Middlebox)
//ELEMENT_MT_SAFE(Middlebox)
