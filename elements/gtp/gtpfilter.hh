#ifndef CLICK_GTPFilter_HH
#define CLICK_GTPFilter_HH
#include <click/batchelement.hh>
#include <click/glue.hh>
#include <clicknet/gtp.h>
CLICK_DECLS

/*
=c

GTPFilter(TEID eid)

=s gtp


=d


*/

class GTPFilter : public BatchElement { public:

    GTPFilter() CLICK_COLD;
    ~GTPFilter() CLICK_COLD;

    const char *class_name() const	{ return "GTPFilter"; }
    const char *port_count() const	{ return PORTS_1_1; }
    const char *flags() const		{ return "A"; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    bool can_live_reconfigure() const	{ return true; }
    void add_handlers() CLICK_COLD;

    Packet *simple_action(Packet *);
#if HAVE_BATCH
	PacketBatch* simple_action_batch(PacketBatch *);
#endif
  private:

    static String read_handler(Element *, void *) CLICK_COLD;

};

CLICK_ENDDECLS
#endif
