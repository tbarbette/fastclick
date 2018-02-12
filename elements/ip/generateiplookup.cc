// -*- c-basic-offset: 4; related-file-name: "generateiplookup.hh" -*-
/*
 * GenerateIPLookup.{cc,hh} -- element generates IPRouteTable patterns out of input traffic
 * Tom Barbette, Georgios Katsikas
 *
 * Copyright (c) 2017 Tom Barbette, University of Liège
 * Copyright (c) 2017 Georgios Katsikas, RISE SICS
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

static const uint8_t DEF_OUT_PORT = 1;

/**
 * IPRouteTable rules' generator out of incoming traffic.
 */
GenerateIPLookup::GenerateIPLookup() :
        _out_port(DEF_OUT_PORT), GenerateIPFilter()
{
    _keep_sport = false;
    _keep_dport = false;
}

GenerateIPLookup::~GenerateIPLookup()
{
}

int
GenerateIPLookup::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
            .read_mp("OUT_PORT", _out_port)
            .consume() < 0)
        return -1;

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
    return GenerateIPFilter::simple_action(p);
}

#if HAVE_BATCH
PacketBatch*
GenerateIPLookup::simple_action_batch(PacketBatch *batch)
{
    return GenerateIPFilter::simple_action_batch(batch);
}
#endif

String
GenerateIPLookup::read_handler(Element *e, void *user_data)
{
    GenerateIPLookup *g = static_cast<GenerateIPLookup *>(e);
    if (!g) {
        return "GenerateIPLookup element not found";
    }

    uint8_t n = 0;
    while (g->_map.size() > g->_nrules) {
        HashTable<IPFlow> new_map;
        ++n;
        g->_mask = IPFlowID(0, 0, IPAddress::make_prefix(32 - n), 0);

        for (auto flow : g->_map) {
            flow.set_mask(g->_mask);
            new_map.find_insert(flow);
        }

        g->_map = new_map;
        if (n == 32) {
            return "Impossible to reduce the number of rules below: " + String(g->_map.size());
        }
    }

    StringAccum acc;

    for (auto flow : g->_map) {
        acc << flow.flowid().daddr() << '/' << String(32 - n) <<
               " " << g->_out_port << ",\n";
    }

    return acc.take_string();
}

void
GenerateIPLookup::add_handlers()
{
    add_read_handler("dump", read_handler);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(GenerateIPFilter)
EXPORT_ELEMENT(GenerateIPLookup)
