#ifndef CLICK_FLOWPAINT_HH
#define CLICK_FLOWPAINT_HH
#include <click/flow/flowelement.hh>
CLICK_DECLS

/*
=c

FlowPaint(COLOR [, ANNO])

=s flow

sets packet FlowPaint annotations

=d

Sets each packet's FlowPaint annotation to COLOR, an integer 0..255.

FlowPaint sets the packet's FlowPaint annotation by default, but the ANNO argument can
specify any one-byte annotation.

=h color read/write

Get/set the color to FlowPaint.

=a FlowPaintTee */

class FlowPaint : public FlowSharedBufferPaintElement { public:

    FlowPaint() CLICK_COLD;

    const char *class_name() const		{ return "FlowPaint"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    bool can_live_reconfigure() const		{ return true; }
    void add_handlers() CLICK_COLD;

    void push_flow(int port, int* flowdata, PacketBatch* head);

  private:

    int _color;

};

CLICK_ENDDECLS
#endif
