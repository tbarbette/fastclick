#ifndef CLICK_IPSUMDUMP_ANNO_HH
#define CLICK_IPSUMDUMP_ANNO_HH
#include "ipsumdumpinfo.hh"
CLICK_DECLS

class IPSummaryDump_Anno { public:

    const char *class_name() const		{ return "IPSummaryDump_Anno"; }

    static void static_initialize();
    static void static_cleanup();
};

CLICK_ENDDECLS
#endif
