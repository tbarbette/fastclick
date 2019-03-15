// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_FLOWSTRIP_HH
#define CLICK_FLOWSTRIP_HH
#include <click/batchelement.hh>

#include <click/flow/flowelement.hh>
CLICK_DECLS

/*
 * =c
 * FlowStrip(LENGTH)
 * =s basicmod
 * FlowStrips bytes from front of packets
 * =d
 * Deletes the first LENGTH bytes from each packet.
 * =e
 * Use this to get rid of the Ethernet header:
 *
 *   FlowStrip(14)
 * =a FlowStripToNetworkHeader, FlowStripIPHeader, EtherEncap, IPEncap, Truncate
 */

class FlowStrip : public FlowElement { public:

    FlowStrip() CLICK_COLD;

    const char *class_name() const		{ return "FlowStrip"; }
    const char *port_count() const		{ return PORTS_1_1; }

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;

    PacketBatch *simple_action_batch(PacketBatch *) override;

    void apply_offset(FlowNode* node, bool invert);

    FlowNode* get_table(int,Vector<FlowElement*> context) override;

    virtual FlowNode* resolveContext(FlowType t, Vector<FlowElement*> stack) override;
  private:

    unsigned _nbytes;

};

CLICK_ENDDECLS
#endif
