#ifndef CLICK_ERSPANDECAP_HH
#define CLICK_ERSPANDECAP_HH
#include <click/batchelement.hh>
#include <click/glue.hh>
CLICK_DECLS

/*
=c

ERSPANDecap()

=s tunnel

decapsulate the ERSPAN packet and set the SPAN ID in the aggregate annotation,
and the timestamp if defined in the timestamp anno

*/

class ERSPANDecap : public ClassifyElement<ERSPANDecap> { public:

    ERSPANDecap() CLICK_COLD;
    ~ERSPANDecap() CLICK_COLD;

    const char *class_name() const	{ return "ERSPANDecap"; }
    const char *port_count() const	{ return PORTS_1_1X2; }
    const char *flags() const		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    bool can_live_reconfigure() const	{ return true; }

    int classify(Packet *);

  private:
	bool _span_anno;
	bool _ts_anno;
    bool _obs_warn;
    bool _direction_anno;

};

CLICK_ENDDECLS
#endif
