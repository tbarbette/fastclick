#ifndef CLICK_NETMAPINFO_HH
#define CLICK_NETMAPINFO_HH

#include <click/element.hh>

CLICK_DECLS

class NetmapInfo : public Element { public:
    const char *class_name() const override	{ return "NetmapInfo"; }

    int configure_phase() const		{ return CONFIGURE_PHASE_FIRST; }

    int configure(Vector<String> &conf, ErrorHandler *errh);

	static NetmapInfo* instance;
};

CLICK_ENDDECLS

#endif
