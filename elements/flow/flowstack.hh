// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_FlowStack_HH
#define CLICK_FlowStack_HH
#include <click/batchelement.hh>

#include <click/flow/flowelement.hh>
CLICK_DECLS

/*
 * =c
 * FlowStack(RELEASE)

 */

class FlowStack : public FlowElement { public:

    FlowStack() CLICK_COLD;

    const char *class_name() const		{ return "FlowStack"; }
    const char *port_count() const		{ return PORTS_1_1; }

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;

    bool stopClassifier() override CLICK_COLD { return true; };

    void push_batch(int port, PacketBatch *) override;

  private:

    bool _release;

};

CLICK_ENDDECLS
#endif
