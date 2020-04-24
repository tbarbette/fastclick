#ifndef CLICK_RandLoad_HH
#define CLICK_RandLoad_HH
#include <click/config.h>
#include <click/glue.hh>
#include <click/vector.hh>
#include <click/batchelement.hh>
#include <random>

CLICK_DECLS

/**
 * RandLoad(MIN, MAX)
 *
 * =s research
 *
 * Generates a constrained random amount of CPU load
 *
 * =d
 *
 * For each packet, this element will generate between MIN+1 and MAX+1 number
 * of pseudo-random numbers to induce some CPU load.
 *
 * =a WorkPackage, FlowRandLoad
 */
class RandLoad : public BatchElement {

public:

    RandLoad() CLICK_COLD;
    ~RandLoad() CLICK_COLD;

    const char *class_name() const		{ return "RandLoad"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
    int initialize(ErrorHandler *errh) override CLICK_COLD;

    void push_batch(int, PacketBatch *) override;
private:
    int _min;
    int _max;
    per_thread<std::mt19937> _gens;
};





CLICK_ENDDECLS
#endif
