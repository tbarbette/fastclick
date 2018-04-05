#ifndef CLICK_ENSUREHEADROOM_HH
#define CLICK_ENSUREHEADROOM_HH
#include <click/batchelement.hh>
CLICK_DECLS

/*
=c

EnsureHeadroom
*/

class EnsureHeadroom : public BatchElement { public:

    EnsureHeadroom() CLICK_COLD;

    const char *class_name() const		{ return "EnsureHeadroom"; }
    const char *port_count() const		{ return PORTS_1_1; }

    int configure(Vector<String> &conf, ErrorHandler *errh) CLICK_COLD;

#if HAVE_BATCH
    PacketBatch* simple_action_batch(PacketBatch*);
#endif
    Packet* simple_action(Packet *);
	Packet* smaction(Packet*);
	private:
   unsigned _headroom;
   bool _force;
};

CLICK_ENDDECLS
#endif
