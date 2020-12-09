#ifndef CLICK_IPSUMDUMP_LINK_HH
#define CLICK_IPSUMDUMP_LINK_HH
#include "ipsumdumpinfo.hh"
CLICK_DECLS

class IPSummaryDump_Link { public:

    const char *class_name() const		{ return "IPSummaryDump_Link"; }

    static void static_initialize();
    static void static_cleanup();
};

CLICK_ENDDECLS
#endif
