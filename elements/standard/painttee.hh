#ifndef CLICK_PAINTTEE_HH
#define CLICK_PAINTTEE_HH
#include <click/batchelement.hh>
CLICK_DECLS

/*
 * =c
 * PaintTee(COLOR [, ANNO])
 * =s paint
 * duplicates packets with given paint annotation
 * =d
 *
 * PaintTee sends every packet through output 0. If the packet's
 * paint annotation is equal to COLOR (an integer), it also
 * sends a copy through output 1.
 *
 * PaintTee uses the PAINT annotation by default, but the ANNO argument can
 * specify any one-byte annotation.
 *
 * =e
 * Intended to produce redirects in conjunction with Paint and
 * ICMPError as follows:
 *
 *   FromDevice(eth7) -> Paint(7) -> ...
 *   routingtable[7] -> pt :: PaintTee(7) -> ... -> ToDevice(eth7)
 *   pt[1] -> ICMPError(18.26.4.24, 5, 1) -> [0]routingtable;
 *
 * =a Paint, ICMPError
 */

class PaintTee : public BatchElement { public:

    PaintTee() CLICK_COLD;

    const char *class_name() const override	{ return "PaintTee"; }
    const char *port_count() const override	{ return "1/2"; }
    const char *processing() const override	{ return PROCESSING_A_AH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    void add_handlers() CLICK_COLD;

    Packet *simple_action(Packet *);
#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch *);
#endif

  private:

    uint8_t _anno;
    uint8_t _color;

};

CLICK_ENDDECLS
#endif
