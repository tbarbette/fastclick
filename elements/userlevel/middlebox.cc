#include <click/config.h>
#include "middlebox.hh"
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include <click/args.hh>
#include <click/error.hh>
CLICK_DECLS

Middlebox::Middlebox()
{
}

int
Middlebox::configure(Vector<String> &conf, ErrorHandler *errh)
{
  //return Args(conf, this, errh).read_mp("LENGTH", _max).complete();
}

void
Middlebox::push(int, Packet *packet)
{
  WritablePacket *p = packet->uniqueify();
  processPacket(p);
  output(0).push(p);
}

Packet *
Middlebox::pull(int)
{
  Packet *packet = input(0).pull();

  click_chatter("Pulled packet");
  WritablePacket *p = packet->uniqueify();
  processPacket(p);
  return p;
}

void
Middlebox::processPacket(WritablePacket* packet)
{
  click_ip *iph = packet->ip_header();
  click_tcp *tcph = packet->tcp_header();


	// Check if the packet is a TCP packet
  if(!packet->has_network_header() || iph->ip_p != IP_PROTO_TCP)
    return;

  unsigned tcph_len = tcph->th_off << 2;
  unsigned char *tcp_payload = packet->transport_header() + tcph_len;
  uint32_t payload_length = packet->length() - (packet->transport_header_offset() + tcph_len);

  // Check if the payload has a length of 1, which is the case with telnet
  if(payload_length == 1)
  {
    // Process payload
    processContent(tcp_payload);

    // Recompute checksum
    unsigned csum;
    unsigned plen = ntohs(iph->ip_len) - (iph->ip_hl << 2);
    tcph->th_sum = 0;
    csum = click_in_cksum((unsigned char *)tcph, plen);
    tcph->th_sum = click_in_cksum_pseudohdr(csum, iph, plen);
  }
}

void Middlebox::processContent(unsigned char* content)
{
  char *c = (char *)&(content[0]);

  // Lower case
  if ((*c >= 65) && (*c <= 90))
    *c = *c + 32; 

  // Change azerty input to qwerty
  switch(*c)
  {
    case 'a':
      *c = 'q';
    break;
    case 'z':
      *c = 'w';
    break;
    case ',':
      *c = 'm';
    break;
    case 'm':
      *c = ';';
    break;
  }
}

CLICK_ENDDECLS
EXPORT_ELEMENT(Middlebox)
//ELEMENT_MT_SAFE(Middlebox)
