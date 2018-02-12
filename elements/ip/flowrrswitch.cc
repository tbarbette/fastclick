// -*- c-basic-offset: 4; related-file-name: "flowrrswitch.hh" -*-
/*
 * FlowRRSwitch.{cc,hh} -- element splits input flows across its ports
 * using a round-robin scheme.
 * Georgios Katsikas
 *
 * Copyright (c) 2018 Georgios Katsikas, RISE SICS
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/straccum.hh>

#include "flowrrswitch.hh"

CLICK_DECLS

FlowRRSwitch::FlowRRSwitch() : _max(0), _current_port(0), _map(), _mask()
{
}

FlowRRSwitch::~FlowRRSwitch()
{
}

int
FlowRRSwitch::configure(Vector<String> &conf, ErrorHandler *errh)
{
    _max = noutputs();
    _mask = IPFlowID(0xffffffff, 0xffff, 0xffffffff, 0xffff);
}

void
FlowRRSwitch::cleanup(CleanupStage)
{
    _map.clear();
}

uint8_t
FlowRRSwitch::round_robin()
{
    _current_port = (_current_port % _max);
    return _current_port;
}

int
FlowRRSwitch::process(int port, Packet *p)
{
    // Create a flow signature for this packet
    IPFlowID id(p);
    IPFlow new_flow = IPFlow(
        p->ip_header()->ip_p,   // Protocol
        p->length()             // and packet length
    );
    new_flow.initialize(id & _mask);

    // Check if we already have such a flow
    IPFlow *found = _map.find(id).get();

    // New flow
    if (!found) {
        // This is where the flow will be emitted
        new_flow.set_output_port(_current_port);

        // Insert this new flow into the flow map
        _map.find_insert(new_flow);

        // Set the destiny of the next flow
        round_robin();
    } else {
        // Update this existing flow
        found->update_size(p->length());
    }

    return new_flow.output_port();
}

void
FlowRRSwitch::push(int port, Packet *p)
{
    output(process(port, p)).push(p);
}

#if HAVE_BATCH
void
FlowRRSwitch::push_batch(int port, PacketBatch *batch)
{
    auto fnt = [this, port](Packet *p) { return process(port, p); };
    CLASSIFY_EACH_PACKET(_max + 1, fnt, batch, checked_output_push_batch);
}
#endif

String
FlowRRSwitch::read_handler(Element *e, void *user_data)
{
    FlowRRSwitch *r = static_cast<FlowRRSwitch *>(e);
    if (!r) {
        return "FlowRRSwitch element not found";
    }
    intptr_t what = reinterpret_cast<intptr_t>(user_data);

    switch (what) {
        case h_flow_count: {
            return String(r->_map.size());
        }
        default: {
            click_chatter("Unknown read handler: %d", what);
            return "";
        }
    }
}

void
FlowRRSwitch::add_handlers()
{
    add_read_handler("flow_count", read_handler, h_flow_count);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(FlowRRSwitch)
