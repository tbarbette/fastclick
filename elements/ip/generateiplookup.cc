// -*- c-basic-offset: 4; related-file-name: "generateiplookup.hh" -*-
/*
 * generateiplookup.{cc,hh} -- element generates IPRouteTable rule patterns out of input traffic
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

    if (GenerateIPFilter::configure(conf, errh) < 0) {
        return -1;
    }

    // No matter what the user requested, these flags must be reset because rotung solely depends on dst IPs.
    if (_keep_saddr) {
        _keep_saddr = false;
        errh->warning("KEEP_SADDR cannot be true as routing IP lookup operations are solely based on destination IPs.");
    }
    if (_keep_sport) {
        _keep_sport = false;
        errh->warning("KEEP_SPORT cannot be true as routing IP lookup operations are solely based on destination IPs.");
    }
    if (_keep_dport) {
        _keep_dport = false;
        errh->warning("KEEP_SPORT cannot be true as routing IP lookup operations are solely based on destination IPs.");
    }

    int status = build_mask(_mask, _keep_saddr, _keep_daddr, _keep_sport, _keep_dport, _prefix);
    if (status != 0) {
        return errh->error("Cannot continue with empty mask");
    }

    // Create the supported IPLookup rule formatter
    _rule_formatter_map.insert(
        static_cast<uint8_t>(RULE_IPLOOKUP),
        new IPLookupRuleFormatter(_out_port, _keep_sport, _keep_dport));

    return 0;
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

IPFlowID
GenerateIPLookup::get_mask(int prefix)
{
    IPFlowID fid = IPFlowID(0, 0, IPAddress::make_prefix(prefix), 0);
    return fid;
}

bool
GenerateIPLookup::is_wildcard(const IPFlow &flow)
{
    if (flow.flowid().daddr().s() == "0.0.0.0") {
        return true;
    }

    return false;
}

String
GenerateIPLookup::read_handler(Element *e, void *user_data)
{
    GenerateIPLookup *g = static_cast<GenerateIPLookup *>(e);
    if (!g) {
        return "GenerateIPLookup element not found";
    }

    assert(g->_pattern_type == IPLOOKUP);

    intptr_t what = reinterpret_cast<intptr_t>(user_data);

    switch (what) {
        case h_flows_nb: {
            return String(g->_flows_nb);
        }
        case h_rules_nb: {
            return String(g->count_rules());
        }
        case h_dump: {
            return g->dump_rules(RULE_IPLOOKUP, true);
        }
        default: {
            click_chatter("Unknown read handler: %d", what);
            return "";
        }
    }
}

String
GenerateIPLookup::to_file_handler(Element *e, void *user_data)
{
    GenerateIPLookup *g = static_cast<GenerateIPLookup *>(e);
    if (!g) {
        return "GenerateIPLookup element not found";
    }

    intptr_t what = reinterpret_cast<intptr_t>(user_data);

    String rules = g->dump_rules(RULE_IPLOOKUP, true);
    if (rules.empty()) {
        click_chatter("No rules to write to file: %s", g->_out_file.c_str());
        return "";
    }

    if (g->dump_rules_to_file(rules) != 0) {
        return "";
    }

    return "";
}

String
IPLookupRuleFormatter::flow_to_string(GenerateIPPacket::IPFlow &flow, const uint32_t flow_nb, const uint8_t prefix)
{
    assert((prefix > 0) && (prefix <= 32));

    StringAccum acc;

    acc << flow.flowid().daddr() << '/' << (int) prefix << " " << (int) _out_port << ",\n";

    return acc.take_string();
}

void
GenerateIPLookup::add_handlers()
{
    add_read_handler("flows_nb", read_handler, h_flows_nb);
    add_read_handler("rules_nb", read_handler, h_rules_nb);
    add_read_handler("dump", read_handler, h_dump);
    add_read_handler("dump_to_file", to_file_handler, h_dump_to_file);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(GenerateIPFilter)
EXPORT_ELEMENT(GenerateIPLookup)
