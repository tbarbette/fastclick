#ifndef CLICK_StripTransportHeader_HH
#define CLICK_StripTransportHeader_HH
#include <click/batchelement.hh>
#include <clicknet/tcp.h>
#include <clicknet/udp.h>
#include <clicknet/icmp.h>
CLICK_DECLS

/*
 * =c
 * StripTransportHeader()
 * =s ip
 * strips outermost IP header
 * =d
 *
 * Strips the outermost Transport header from IP packets, based on the Transport Header
 * annotation.
 *
 * Note that the packet's annotations are not changed.  Thus, the packet's transport
 * header annotation continues to point at the transport header, even though the transport
 * header's data is now out of range.  To correctly handle an IP-in-IP packet,
 * you will probably need to follow StripTransportHeader with a CheckIPHeader or
 * MarkIPHeader element, thus marking the packet's inner header.
 *
 * =a CheckIPHeader, CheckIPHeader2, MarkIPHeader, UnStripTransportHeader, Strip
 */

class StripTransportHeader : public BatchElement { public:

    StripTransportHeader() CLICK_COLD;
    ~StripTransportHeader() CLICK_COLD;

    const char *class_name() const override		{ return "StripTransportHeader"; }
    const char *port_count() const override		{ return PORTS_1_1; }

    Packet      *simple_action      (Packet      *p);
#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch *batch);
#endif

    static inline unsigned transport_header_length(Element* e, Packet* p) {
        unsigned l = 0;
        if (p->ip_header()->ip_p == IP_PROTO_TCP) {
            const click_tcp* th = p->tcp_header();
            l = th->th_off << 2;
        } else if (p->ip_header()->ip_p == IP_PROTO_UDP) {
            l = sizeof(click_udp);
        } else if (p->ip_header()->ip_p == IP_PROTO_ICMP) {
            l = sizeof(click_icmp);
        } else {
            click_chatter("%p{element}: Unsupported protocol %d", e,  p->ip_header()->ip_p);
            return 0;
        }
        return l;
    }
};

CLICK_ENDDECLS
#endif
