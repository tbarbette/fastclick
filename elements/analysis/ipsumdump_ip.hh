#ifndef CLICK_IPSUMDUMP_IP_HH
#define CLICK_IPSUMDUMP_IP_HH
#include "ipsumdumpinfo.hh"
CLICK_DECLS

class IPSummaryDump_IP { public:

    const char *class_name() const		{ return "IPSummaryDump_IP"; }

    static void static_initialize();
    static void static_cleanup();
};

CLICK_ENDDECLS
#endif
