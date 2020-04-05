#ifndef CLICK_FlowAcquire_HH
#define CLICK_FlowAcquire_HH
#include <click/flow/flowelement.hh>
CLICK_DECLS

/*
=c

FlowAcquire()

=s FlowAcquire

sets packet FlowAcquire annotations

=d


=a FlowRelease */

class FlowAcquire : public FlowSpaceElement<bool> { public:

    FlowAcquire() CLICK_COLD;

    const char *class_name() const		{ return "FlowAcquire"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    bool can_live_reconfigure() const		{ return true; }
    void add_handlers() CLICK_COLD;

    void push_batch(int port, bool* flowdata, PacketBatch* head);

  private:

    int _color;

};

CLICK_ENDDECLS
#endif
