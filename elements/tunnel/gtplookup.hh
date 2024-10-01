#ifndef CLICK_GTPLookup_HH
#define CLICK_GTPLookup_HH
#include <click/batchelement.hh>
#include <click/ipflowid.hh>
#include <click/hashtablemp.hh>
CLICK_DECLS


/*
=c

GTPLookup()

=s tunnel

Encapsulates packets in their intended GTP return id.

=d

Finds from the 5 tuple of a packet returning from the MEC the right
GTP return ID.

=a GTPEncap
*/

class GTPTable;

class GTPLookup : public BatchElement { public:

    GTPLookup() CLICK_COLD;
    ~GTPLookup() CLICK_COLD;

    const char *class_name() const override	{ return "GTPLookup"; }
    const char *port_count() const override	{ return "1/1"; }
    const char *flow_code() const override  { return "x/x"; }
    const char *flags() const		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    bool can_live_reconfigure() const	{ return true; }

    bool run_task(Task*) override;

    int process(int, Packet*);
    void push(int, Packet *) override;
#if HAVE_BATCH
	void push_batch(int port, PacketBatch *) override;
#endif
  private:
	GTPTable *_table;
    bool _checksum;
    atomic_uint32_t _id;
    per_thread<Packet*> _queue;


};

CLICK_ENDDECLS
#endif
