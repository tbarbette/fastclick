#ifndef CLICK_HASHSWITCH_HH
#define CLICK_HASHSWITCH_HH
#include <click/batchelement.hh>
CLICK_DECLS

/*
 * =c
 * HashSwitch(OFFSET, LENGTH)
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
 *   HashSwitch(16, 4)
 * =a
 * Switch, RoundRobinSwitch, StrideSwitch, RandomSwitch
 */

class HashSwitch : public BatchElement {

    int _offset;
    int _length;
    int _max;

 public:

    HashSwitch() CLICK_COLD;

    const char *class_name() const override        { return "HashSwitch"; }
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
