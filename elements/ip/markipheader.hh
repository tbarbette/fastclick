#ifndef CLICK_MARKIPHEADER_HH
#define CLICK_MARKIPHEADER_HH
#include <click/batchelement.hh>
CLICK_DECLS

/*
 * =c
 * MarkIPHeader([OFFSET])
 * =s ip
 * sets IP header annotation
 * =d
 *
 * Marks packets as IP packets by setting the IP Header annotation. The IP
 * header starts OFFSET bytes into the packet. Default OFFSET is 0.
 *
 * Does not check length fields for sanity, shorten packets to the IP length,
 * or set the destination IP address annotation. Use CheckIPHeader or
 * CheckIPHeader2 for that.
 *
 * =a CheckIPHeader, CheckIPHeader2, StripIPHeader */

class MarkIPHeader : public SimpleElement<MarkIPHeader> {
    public:
        MarkIPHeader () CLICK_COLD;
        ~MarkIPHeader() CLICK_COLD;

        const char *class_name() const override { return "MarkIPHeader"; }
        const char *port_count() const override { return PORTS_1_1; }
        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

        Packet *simple_action(Packet *p);

    private:
        int _offset;
};

CLICK_ENDDECLS
#endif
