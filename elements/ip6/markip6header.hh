#ifndef CLICK_MARKIP6HEADER_HH
#define CLICK_MARKIP6HEADER_HH
#include <click/batchelement.hh>
CLICK_DECLS

/*
 * =c
 * MarkIP6Header([OFFSET])
 * =s ip6
 *
 * =d
 *
 * Marks packets as IP6 packets by setting the IP6 Header annotation. The IP6
 * header starts OFFSET bytes into the packet. Default OFFSET is 0. Does not
 * check length fields for sanity or shorten packets to the IP length; use
 * CheckIPHeader or CheckIPHeader2 for that.
 *
 * =a CheckIP6Header, CheckIP6Header2, StripIP6Header */

class MarkIP6Header : public SimpleElement<MarkIP6Header> {
    public:
        MarkIP6Header();
        ~MarkIP6Header();

        const char *class_name() const override { return "MarkIP6Header"; }
        const char *port_count() const override { return PORTS_1_1; }
        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

        Packet *simple_action(Packet *p);

    private:
        int _offset;
};

CLICK_ENDDECLS
#endif
