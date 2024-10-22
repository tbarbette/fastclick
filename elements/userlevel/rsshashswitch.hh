#ifndef CLICK_RSSHashSwitch_HH
#define CLICK_RSSHashSwitch_HH
#include <click/batchelement.hh>
CLICK_DECLS

/*
 * =c
 * RSSHashSwitch(OFFSET, LENGTH)
 * =s classification
 * classifies packets by hash of contents
 * =d
 * Can have any number of outputs.
 * Chooses the output on which to emit each packet based on
 * a hash of the LENGTH bytes starting at OFFSET.
 * Could be used for stochastic fair queuing.
 * =e
 * This element expects IP packets and chooses the output
 * based on a hash of the IP destination address:
 *
 *   RSSHashSwitch(16, 4)
 * =a
 * Switch, RoundRobinSwitch, StrideSwitch, RandomSwitch
 */

class RSSHashSwitch : public BatchElement {

    int _max;

 public:

    RSSHashSwitch() CLICK_COLD;

    const char *class_name() const override        { return "RSSHashSwitch"; }
    const char *port_count() const override        { return "1/1-"; }
    const char *processing() const override        { return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    int process(Packet *);
    void push(int port, Packet *);
#if HAVE_BATCH
    void push_batch(int port, PacketBatch *);
#endif

};

CLICK_ENDDECLS

#endif
