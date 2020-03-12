#ifndef CLICK_IPSUMDUMP_UDP_HH
#define CLICK_IPSUMDUMP_UDP_HH
#include "ipsumdumpinfo.hh"
CLICK_DECLS

class IPSummaryDump_UDP { public:

    const char *class_name() const		{ return "IPSummaryDump_UDP"; }

    static void static_initialize();
    static void static_cleanup();
};

CLICK_ENDDECLS
#endif
