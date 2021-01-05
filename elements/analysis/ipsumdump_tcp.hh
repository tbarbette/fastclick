#ifndef CLICK_IPSUMDUMP_TCP_HH
#define CLICK_IPSUMDUMP_TCP_HH
#include "ipsumdumpinfo.hh"
CLICK_DECLS

class IPSummaryDump_TCP { public:

    const char *class_name() const		{ return "IPSummaryDump_TCP"; }

    static void static_initialize();
    static void static_cleanup();
};

CLICK_ENDDECLS
#endif
