#ifndef CLICK_TestFlowSpace_HH
#define CLICK_TestFlowSpace_HH
#include <click/config.h>
#include <click/multithread.hh>
#include <click/hashtablemp.hh>
#include <click/glue.hh>
#include <click/vector.hh>
#include <click/flow/flowelement.hh>
#include <random>

CLICK_DECLS

struct FourBytes {
    FourBytes() : w(0) {

	}
	uint32_t w;
};

/**
 * Flow element that asks for 4 bytes
 */
class TestFlowSpace : public FlowSpaceElement<FourBytes> {

public:

    TestFlowSpace() CLICK_COLD;
    ~TestFlowSpace() CLICK_COLD;

    const char *class_name() const override		{ return "TestFlowSpace"; }
    const char *port_count() const override		{ return "1/1"; }
    const char *processing() const override		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
    int initialize(ErrorHandler *errh) override CLICK_COLD;

    void push_flow(int, FourBytes*, PacketBatch *);

};





CLICK_ENDDECLS
#endif
