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
        _out_port(DEF_OUT_PORT), GenerateIPFilter(IPLOOKUP)
{
    _keep_saddr = false;
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

    if (_out_port < 0) {
        errh->error("Invalid OUT_PORT. Input a non-negative integer.");
        return -1;
    }

    int status = build_mask(_mask, _keep_saddr, _keep_daddr, _keep_sport, _keep_dport, _prefix);
    if (status != 0) {
        return errh->error("Cannot continue with empty mask");
    }

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

String
GenerateIPLookup::dump_rules(bool verbose)
{
    uint8_t n = 0;
    while (_map.size() > _nrules) {
        HashTable<IPFlow> new_map;

        if (verbose) {
            click_chatter("%8d rules with prefix /%02d, continuing with /%02d", _map.size(), 32-n, 32-n-1);
        }

        ++n;
        _mask = IPFlowID(0, 0, IPAddress::make_prefix(32 - n), 0);

        for (auto flow : _map) {
            // Wildcards are intentionally excluded
            if ((flow.flowid().daddr().s() == "0.0.0.0")) {
                continue;
            }

            flow.set_mask(_mask);
            new_map.find_insert(flow);
        }

        _map = new_map;
        if (n == 32) {
            return "Impossible to reduce the number of rules below: " + String(_map.size());
        }
    }

    if (verbose) {
        click_chatter("%8d rules with prefix /%02d", _map.size(), 32-n);
    }

    StringAccum acc;

    for (auto flow : _map) {
        acc << flow.flowid().daddr() << '/' << String(32 - n) << " " << (int) _out_port << ",\n";
    }

    return acc.take_string();
}

String
GenerateIPLookup::read_handler(Element *e, void *user_data)
{
    GenerateIPLookup *g = static_cast<GenerateIPLookup *>(e);
    if (!g) {
        return "GenerateIPLookup element not found";
    }

    assert(g->_pattern_type == IPLOOKUP);
    assert(g->_out_port >= 0);

    intptr_t what = reinterpret_cast<intptr_t>(user_data);

    switch (what) {
        case h_dump: {
            return g->dump_rules(true);
        }
        case h_rules_nb: {
            if (g->_map.size() == 0) {
                g->dump_rules();
            }
            return String(g->_map.size());
        }
        default: {
            click_chatter("Unknown read handler: %d", what);
            return "";
        }
    }
}

void
GenerateIPLookup::add_handlers()
{
    add_read_handler("dump", read_handler, h_dump);
    add_read_handler("rules_nb", read_handler, h_rules_nb);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(GenerateIPFilter)
EXPORT_ELEMENT(GenerateIPLookup)
