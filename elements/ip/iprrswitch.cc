// -*- c-basic-offset: 4; related-file-name: "flowrrswitch.hh" -*-
/*
 * flowrrswitch.{cc,hh} -- element splits input flows across its ports
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

#include "iprrswitch.hh"

CLICK_DECLS

IPRRSwitch::IPRRSwitch() : _max_nb_port(0), _current_port(0), _map(), _mask()
{
}

IPRRSwitch::~IPRRSwitch()
{
}

int
IPRRSwitch::configure(Vector<String> &conf, ErrorHandler *errh)
{
    _max_nb_port = noutputs();
    if (Args(conf, this, errh)
        .read("MAX", _max_nb_port)
        .complete() < 0)
    return -1;

    _mask = IPFlowID(0xffffffff, 0xffff, 0xffffffff, 0xffff);

    return 0;
}

void
IPRRSwitch::cleanup(CleanupStage)
{
    _map.clear();
}

unsigned
IPRRSwitch::round_robin()
{
    return _current_port = ((++_current_port) % _max_nb_port);
}

int
IPRRSwitch::process(int port, Packet *p)
{
    // Create a flow signature for this packet
    IPFlowID id(p);
    IPFlowPort new_flow = IPFlowPort();
    new_flow.initialize(id & _mask);

    // Check if we already have such a flow
    IPFlowPort *found = _map.find(id).get();

    // New flow
    if (!found) {
        // This is where the flow will be emitted
        new_flow.set_output_port(round_robin());

        // Insert this new flow into the flow map
        _map.find_insert(new_flow);

        // Set the destiny of the next flow
        return new_flow.output_port();
    }

    return found->output_port();
}

void
IPRRSwitch::push(int port, Packet *p)
{
    output(process(port, p)).push(p);
}

#if HAVE_BATCH
void
IPRRSwitch::push_batch(int port, PacketBatch *batch)
{
    auto fnt = [this, port](Packet *p) { return process(port, p); };
    CLASSIFY_EACH_PACKET(_max_nb_port, fnt, batch, output_push_batch);
}
#endif

String
IPRRSwitch::read_handler(Element *e, void *user_data)
{
    IPRRSwitch *r = static_cast<IPRRSwitch *>(e);
    if (!r) {
        return "IPRRSwitch element not found";
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
IPRRSwitch::add_handlers()
{
    add_read_handler("flow_count", read_handler, h_flow_count);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IPRRSwitch)
