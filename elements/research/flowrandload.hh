#ifndef CLICK_FlowRandLoad_HH
#define CLICK_FlowRandLoad_HH
#include <click/config.h>
#include <click/multithread.hh>
#include <click/hashtablemp.hh>
#include <click/glue.hh>
#include <click/vector.hh>
#include <click/flow/flowelement.hh>
#include <random>

CLICK_DECLS

struct RandLoadState {
	RandLoadState() : w(0) {
	}

	int w;
};

/*
 * =c
 * FlowRandLoad([MIN, MAX])
 *
 * =s research
 * 
 * Artificial CPU load, randomly selected per-flow
 *
 * =d
 *
 * For each new flow (using the flow subsystem) this element will select
 * a random number between MIN and MAX, that designates how many
 * PRNG should be done per-packet. Hence, some flow will appear heavy,
 * and some light. One PRNG is around 8 CPU cycles.
 *
 * Keyword arguments are:
 *
 * =over 8
 *
 * =item MIN
 *
 * Integer. Minimal number of PRNG to run for each packets. Default is 1.
 *
 * =item MAX
 *
 * Integer. Maximal number of PRNG to run for each packets. Default is 100.
 *
 * =e
 *  FlowIPManager->FlowRandLoad(MIN 1, MAX 100).
 *
 * =a
 * RandLoad, WorkPackage
 */
class FlowRandLoad : public FlowSpaceElement<RandLoadState> {

public:

    FlowRandLoad() CLICK_COLD;
    ~FlowRandLoad() CLICK_COLD;

    const char *class_name() const override		{ return "FlowRandLoad"; }
    const char *port_count() const override		{ return "1/1"; }
    const char *processing() const override		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
    int initialize(ErrorHandler *errh) override CLICK_COLD;

    void push_flow(int, RandLoadState*, PacketBatch *);

private:
    int _min;
    int _max;
    per_thread<std::mt19937> _gens;
};





CLICK_ENDDECLS
#endif
