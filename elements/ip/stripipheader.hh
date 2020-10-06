#ifndef CLICK_STRIPIPHEADER_HH
#define CLICK_STRIPIPHEADER_HH
#include <click/batchelement.hh>
CLICK_DECLS

/*
 * =c
 * StripIPHeader()
 * =s ip
 * strips outermost IP header
 * =d
 *
 * Strips the outermost IP header from IP packets, based on the IP Header
 * annotation.
 *
 * Note that the packet's annotations are not changed.  Thus, the packet's IP
 * header annotation continues to point at the IP header, even though the IP
 * header's data is now out of range.  To correctly handle an IP-in-IP packet,
 * you will probably need to follow StripIPHeader with a CheckIPHeader or
 * MarkIPHeader element, thus marking the packet's inner header.
 *
 * =a CheckIPHeader, CheckIPHeader2, MarkIPHeader, UnstripIPHeader, Strip
 */

class StripIPHeader : public BatchElement { public:

    StripIPHeader() CLICK_COLD;
    ~StripIPHeader() CLICK_COLD;

    const char *class_name() const override		{ return "StripIPHeader"; }
    const char *port_count() const override		{ return PORTS_1_1; }

    Packet      *simple_action      (Packet      *p);
#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch *batch);
#endif
};

CLICK_ENDDECLS
#endif
