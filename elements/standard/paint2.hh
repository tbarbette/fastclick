#ifndef CLICK_Paint2_HH
#define CLICK_Paint2_HH
#include <click/element.hh>
CLICK_DECLS

/*
=c

Paint2(COLOR [, ANNO])

=s Paint2

sets packet Paint2 annotations

=d

Sets each packet's PAINT2 annotation to COLOR, an integer 0..255.

Paint2 sets the packet's PAINT2 annotation by default, but the ANNO argument can
specify any two-byte annotation.

=h color read/write

Get/set the color to PAINT2.

=a Paint */

class Paint2 : public Element { public:

    Paint2() CLICK_COLD;

    const char *class_name() const override		{ return "Paint2"; }
    const char *port_count() const override		{ return PORTS_1_1; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    bool can_live_reconfigure() const		{ return true; }
    void add_handlers() CLICK_COLD;

    Packet *simple_action(Packet *);

  private:

    uint8_t _anno;
    uint16_t _color;

};

CLICK_ENDDECLS
#endif
