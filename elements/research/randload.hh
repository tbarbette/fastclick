#ifndef CLICK_RANDLOAD_HH
#define CLICK_RANDLOAD_HH
#include <click/config.h>
#include <click/glue.hh>
#include <click/vector.hh>
#include <click/batchelement.hh>
#include <random>

CLICK_DECLS
/*
 * =c
 * RandLoad([MIN, MAX])
 *
 * =s research
 *
 * Random artificial CPU load for each packet
 *
 * =d
 *
 * For each packet this element will select a random number between MIN and
 * MAX, that designates how many PRNG should be done.
 * One PRNG is around 8 CPU cycles.
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
 *  RandLoad(MIN 1, MAX 100).
 *
 * =a
 * FlowRandLoad, WorkPackage
 */
class RandLoad : public BatchElement {

public:

    RandLoad() CLICK_COLD;
    ~RandLoad() CLICK_COLD;

    const char *class_name() const override		{ return "RandLoad"; }
    const char *port_count() const override		{ return "1/1"; }
    const char *processing() const override		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh);

    void push_batch(int, PacketBatch *);

private:
    int _min;
    int _max;
    per_thread<std::mt19937> _gens;

};





CLICK_ENDDECLS
#endif
