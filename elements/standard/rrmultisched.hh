// -*- c-basic-offset: 4 -*-
#ifndef CLICK_RRMULTISCHED_HH
#define CLICK_RRMULTISCHED_HH
#include <click/notifier.hh>
#include "rrsched.hh"
CLICK_DECLS

/*
 * =c
 * RoundRobinMultiSched
 * =s scheduling
 * pulls from round-robin inputs, a given amount of times each input
 * =io
 * one output, zero or more inputs
 * =d
 * Each time a pull comes in the output, pulls from its inputs
 * in turn until one produces a packet. It will retry this same
 * input N times. When the next pull comes in, it starts from the
 * input after the one that last produced a packet.
 *
 * The inputs usually come from Queues or other pull schedulers.
 * RoundRobinSched uses notification to avoid pulling from empty inputs.
 *
 * =a PrioSched, StrideSched, DRRMultiSched, RoundRobinSwitch, SimpleRoundRobinSched, RoundRobinSched
 */

class RRMultiSched : public RRSched {
    public:
        RRMultiSched() CLICK_COLD;

        const char *class_name() const override { return "RoundRobinMultiSched"; }
        int configure(Vector<String> &conf, ErrorHandler *) CLICK_COLD;
        Packet *pull(int port);
    #if HAVE_BATCH
        PacketBatch *pull_batch(int port, unsigned max) override;
    #endif

    private:
        int _n;
        int _n_cur;

};

CLICK_ENDDECLS
#endif
