// -*- c-basic-offset: 4; related-file-name: "generateiplookup.hh" -*-
/*
 * GenerateIPLookup.{cc,hh} -- element generates IPRouteTable patterns out of input traffic
 * Tom Barbette, Georgios Katsikas
 *
 * Copyright (c) 2017 Tom Barbette, University of Liège
 * Copyright (c) 2017 Georgios Katsikas, RISE SICS AB
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

#include "generateiplookup.hh"

CLICK_DECLS

const int GenerateIPLookup::DEF_OUT_PORT = 1;
int GenerateIPLookup::_out_port = DEF_OUT_PORT;

/**
 * IPRouteTable rules' generator out of incoming traffic.
 */
GenerateIPLookup::GenerateIPLookup() : GenerateIPPacket()
{
}

GenerateIPLookup::~GenerateIPLookup()
{
}

int
GenerateIPLookup::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
            .read_p("OUT_PORT", _out_port)
            .consume() < 0)
        return -1;

    if (_out_port < 0) {
        _out_port = DEF_OUT_PORT;
        errh->warning("OUT_PORT is set to default value: %d", _out_port);
    }

    // Routers care about destination IP addresses
    _mask = IPFlowID(0, 0, 0xffffffff, 0);

    return GenerateIPPacket::configure(conf, errh);
}

int
GenerateIPLookup::initialize(ErrorHandler *errh)
{
    return GenerateIPPacket::initialize(errh);
}

void
GenerateIPLookup::cleanup(CleanupStage)
{
}

Packet *
GenerateIPLookup::simple_action(Packet *p)
{
    IPFlowID flowid(p);
    IPFlow flow = IPFlow();
    flow.initialize(flowid & _mask);
    _map.find_insert(flow);

    return p;
}

#if HAVE_BATCH
PacketBatch*
GenerateIPLookup::simple_action_batch(PacketBatch *batch)
{
    EXECUTE_FOR_EACH_PACKET(simple_action, batch);
    return batch;
}
#endif

String
GenerateIPLookup::read_handler(Element *e, void *user_data)
{
    GenerateIPLookup *g = static_cast<GenerateIPLookup *>(e);
    if (!g) {
        return "GenerateIPLookup element not found";
    }

    StringAccum acc;

    int n = 0;
    while (g->_map.size() > g->_nrules) {
        HashTable<IPFlow> newmap;
        ++n;
        g->_mask = IPFlowID(0, 0, IPAddress::make_prefix(32 - n), 0);

        for (auto flow : g->_map) {
            flow.setMask(g->_mask);
            newmap.find_insert(flow);
        }

        g->_map = newmap;
        if (n == 32) {
            return "Impossible to lower the rules and keep the choosen fields";
        }
    }

    for (auto flow : g->_map) {
        acc << flow.flowid().daddr() << '/' << String(32 - n) << " " << _out_port << ",\n";
    }

    return acc.take_string();
}

void
GenerateIPLookup::add_handlers()
{
    add_read_handler("dump", read_handler);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(GenerateIPLookup)
