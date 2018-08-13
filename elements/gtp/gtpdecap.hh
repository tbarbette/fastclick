#ifndef CLICK_GTPDECAP_HH
#define CLICK_GTPDECAP_HH
#include <click/batchelement.hh>
#include <click/glue.hh>
#include <clicknet/gtp.h>
CLICK_DECLS

/*
=c

GTPDecap()

decapsulates GTP packet

=s gtp

=d

decapsulates the GTP packet and set the GTP TEID in the aggregate annotation

=a GTPEncap
*/

class GTPDecap : public BatchElement { public:

    GTPDecap() CLICK_COLD;
    ~GTPDecap() CLICK_COLD;

    const char *class_name() const	{ return "GTPDecap"; }
    const char *port_count() const	{ return PORTS_1_1; }
    const char *flags() const		{ return "A"; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    bool can_live_reconfigure() const	{ return true; }

    Packet *simple_action(Packet *);
#if HAVE_BATCH
	PacketBatch* simple_action_batch(PacketBatch *);
#endif
  private:
	bool _anno;

};

CLICK_ENDDECLS
#endif
