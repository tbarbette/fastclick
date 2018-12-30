#ifndef CLICK_PAINTRR_HH
#define CLICK_PAINTRR_HH
#include <click/batchelement.hh>
CLICK_DECLS

/*
=c

PaintRR

=s paint

sends packet stream to one of the output ports chosen per-packet

=d

PaintRR sends every incoming packet to one of its output ports according to
the value of the incoming packet's paint annotation.

The choosen port will be the modulo of the annotation. The difference with
PaintSwitch is therefore that any value can be used and will always lead to
a valid output.

PaintRR uses the PAINT annotation by default, but the ANNO argument can
specify any one-byte annotation.

=a PaintSwitch */

class PaintRR : public ClassifyElement<PaintRR> { public:

    PaintRR() CLICK_COLD;

    const char *class_name() const		{ return "PaintRR"; }
    const char *port_count() const		{ return "1/-"; }
    const char *processing() const		{ return PUSH; }

    int configure(Vector<String> &conf, ErrorHandler *errh) CLICK_COLD;

    inline int classify(Packet *p);

  private:

    uint8_t _anno;

};

CLICK_ENDDECLS
#endif
