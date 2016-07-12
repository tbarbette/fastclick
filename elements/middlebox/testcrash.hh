#ifndef MIDDLEBOX_TESTCRASH_HH
#define MIDDLEBOX_TESTCRASH_HH

#include <click/config.h>
#include <click/element.hh>
#include <click/router.hh>

CLICK_DECLS

class TestCrash : public Element
{
public:
    TestCrash() CLICK_COLD;
    ~TestCrash();

    const char *class_name() const        { return "TestCrash"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    void push(int, Packet *);

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

};

CLICK_ENDDECLS

#endif
