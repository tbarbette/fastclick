// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_TRUNCATEFCS_HH
#define CLICK_TRUNCATEFCS_HH
#include <click/element.hh>
CLICK_DECLS

/*
 * =c
 * TruncateFCS()
 * =s ethernet
 * remove the FCS from packet
 * =d
 * Does not check if the FCS is present, therefore the packets is always shortened by 4 bytes.
 *
 * The EXTRA_LENGTH keyword argument determines whether packets' extra length
 * annotations are updated to account for any dropped bytes.  Default is true.
 * =a Strip, Pad, Truncate
 */

class TruncateFCS : public Element { public:

    TruncateFCS() CLICK_COLD;

    const char *class_name() const override		{ return "TruncateFCS"; }
    const char *port_count() const override		{ return PORTS_1_1; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    bool can_live_reconfigure() const		{ return true; }

    Packet *simple_action(Packet *);

    void add_handlers() CLICK_COLD;

  private:

    bool _extra_anno;

};

CLICK_ENDDECLS
#endif
