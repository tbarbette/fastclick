#include <click/config.h>
#include "middlebox.hh"
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
	/*
  if (p->length() > _max)
    checked_output_push(1, p);
  else
    output(0).push(p);
    */

	click_chatter("Pushed packet");
	processPacket(packet);

	output(0).push(packet);
}

Packet *
Middlebox::pull(int)
{
	/*
  Packet *p = input(0).pull();
  if (p && p->length() > _max) {
    checked_output_push(1, p);
    return 0;
  } else
    return p;
    */
    Packet *packet = input(0).pull();

    click_chatter("Pulled packet");
    processPacket(packet);

    return packet;
}

void
Middlebox::processPacket(Packet* packet)
{
	const click_ip *iph = p->ip_header();
	const click_tcp *tcph = p->tcp_header();

	// Check if the packet is a TCP packet
	if (!p->has_network_header() || iph->ip_p != IP_PROTO_TCP)
		return;

	


}

CLICK_ENDDECLS
EXPORT_ELEMENT(Middlebox)
//ELEMENT_MT_SAFE(Middlebox)
