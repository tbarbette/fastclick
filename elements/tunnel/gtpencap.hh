#ifndef CLICK_GTPENCAP_HH
#define CLICK_GTPENCAP_HH
#include <click/batchelement.hh>
#include <click/glue.hh>
#include <clicknet/gtp.h>
CLICK_DECLS

/*
=c

GTPEncap(TEID eid)

encapsulates GTP packets

=s gtp

=d

encapsulates packets in static GPRS Tunneling Protocol header.
This does not include UDP/IP Encapsulation, but GTP is usually over UDP/IP

Does not support sequences or GTP-C.

=a Strip, UDPIPEncap
*/

class GTPEncap : public BatchElement { public:

    GTPEncap() CLICK_COLD;
    ~GTPEncap() CLICK_COLD;

    const char *class_name() const	{ return "GTPEncap"; }
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

    uint32_t _eid;
    static String read_handler(Element *, void *) CLICK_COLD;

};

CLICK_ENDDECLS
#endif
