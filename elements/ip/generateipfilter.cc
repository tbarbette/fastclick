// -*- c-basic-offset: 4; related-file-name: "generateipfilter.hh" -*-
/*
 * GenerateIPFilter.{cc,hh} -- element generates IPFilter patterns out of input traffic
 * Tom Barbette, (extended by) Georgios Katsikas
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

#include "generateipfilter.hh"
#include "generateiplookup.hh"
#include "generateipflowdispatcher.hh"

CLICK_DECLS

const int GenerateIPPacket::DEF_NB_RULES = 8000;

/**
 * Base class for pattern generation out of incoming traffic.
 */
GenerateIPPacket::GenerateIPPacket() : _nrules(DEF_NB_RULES), _prefix(32)
{
}

GenerateIPPacket::~GenerateIPPacket()
{
}

int
GenerateIPPacket::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
            .read_p("NB_RULES", _nrules)
            .consume() < 0)
        return -1;

  return 0;
}

int
GenerateIPPacket::initialize(ErrorHandler *errh)
{
    return 0;
}

int
GenerateIPPacket::build_mask(IPFlowID &mask, bool keep_saddr, bool keep_daddr, bool keep_sport, bool keep_dport, int prefix)
{
    if (!keep_saddr && !keep_daddr && !keep_sport && !keep_dport) {
        return -1;
    }

     mask = IPFlowID(
        (keep_saddr ? IPAddress::make_prefix(prefix) : IPAddress("")),
        (keep_sport ? 0xffff : 0),
        (keep_daddr ? IPAddress::make_prefix(prefix) : IPAddress("")),
        (keep_dport ? 0xffff : 0)
    );

     return 0;
}

void
GenerateIPPacket::cleanup(CleanupStage)
{
    return;
}

/**
 * IP FIlter rules' generator out of incoming traffic.
 */
GenerateIPFilter::GenerateIPFilter() :
    GenerateIPPacket(), _keep_saddr(true), _keep_daddr(true), _keep_sport(false), _keep_dport(true),
    _pattern_type(NONE)
{
}

GenerateIPFilter::GenerateIPFilter(RulePattern pattern_type) :
    GenerateIPPacket(), _keep_saddr(true), _keep_daddr(true), _keep_sport(false), _keep_dport(true)
{
    _pattern_type = pattern_type;
}

GenerateIPFilter::~GenerateIPFilter()
{
}

int
GenerateIPFilter::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String pattern_type = "IPFILTER";

    if (Args(conf, this, errh)
            .read("KEEP_SADDR", _keep_saddr)
            .read("KEEP_DADDR", _keep_daddr)
            .read("KEEP_SPORT", _keep_sport)
            .read("KEEP_DPORT", _keep_dport)
            .read("PATTERN_TYPE", pattern_type)
            .read("PREFIX", _prefix)
            .consume() < 0)
        return -1;

    int status = build_mask(_mask, _keep_saddr, _keep_daddr, _keep_sport, _keep_dport, _prefix);
    if (status != 0) {
        return errh->error("Cannot continue with empty mask");
    }

    /**
     * Sub-classes of GenerateIPFilter have this member already set.
     * Only GenerateIPFilter is instantiated with NONE, thus we need to set it now.
     */
    if (_pattern_type == NONE) {
        if (pattern_type.upper() == "IPFILTER") {
            _pattern_type = IPFILTER;
        } else if (pattern_type.upper() == "IPCLASSIFIER") {
            _pattern_type = IPCLASSIFIER;
        } else {
            errh->error("Invalid PATTERN_TYPE for GenerateIPFilter. Select in [IPFILTER, IPCLASSIFIER]");
            return -1;
        }
    // GenerateIPLookup sub-class
    } else if (_pattern_type == IPLOOKUP) {
        GenerateIPLookup *lookup_ptr = dynamic_cast<GenerateIPLookup *>(this);
        if (lookup_ptr == NULL) {
            errh->error("Invalid PATTERN_TYPE for GenerateIPLookup.");
            return -1;
        }
    // GenerateIPFlowDispatcher sub-class
    } else if (_pattern_type == FLOW_DISPATCHER) {
        GenerateIPFlowDispatcher *fd_ptr = dynamic_cast<GenerateIPFlowDispatcher *>(this);
        if (fd_ptr == NULL) {
            errh->error("Invalid PATTERN_TYPE for GenerateIPFlowDispatcher.");
            return -1;
        }
    }

    return GenerateIPPacket::configure(conf, errh);
}

int
GenerateIPFilter::initialize(ErrorHandler *errh)
{
    return GenerateIPPacket::initialize(errh);
}

void
GenerateIPFilter::cleanup(CleanupStage)
{
}

Packet *
GenerateIPFilter::simple_action(Packet *p)
{
    // Create a flow signature for this packet
    IPFlowID flowid(p);
    IPFlow new_flow = IPFlow();
    new_flow.initialize(flowid & _mask);

    // Check if we already have such a flow
    IPFlow *found = _map.find(flowid).get();

    // New flow
    if (!found) {
        // Its length is the length of this packet
        new_flow.update_flow_size(p->length());

        // Keep the protocol type
        if (_keep_sport || _keep_dport) {
            new_flow.set_proto(p->ip_header()->ip_p);
        }

        // Insert this new flow into the flow map
        _map.find_insert(new_flow);
    } else {
        // Aggregate
        found->update_flow_size(p->length());
    }

    return p;
}

#if HAVE_BATCH
PacketBatch*
GenerateIPFilter::simple_action_batch(PacketBatch *batch)
{
    EXECUTE_FOR_EACH_PACKET(GenerateIPFilter::simple_action, batch);
    return batch;
}
#endif

String
GenerateIPFilter::dump_rules(bool verbose)
{
    uint8_t n = 32 - _prefix;
    while (_map.size() > (unsigned)_nrules) {
        HashTable<IPFlow> new_map;

        if (verbose) {
            click_chatter("%8d rules with prefix /%02d, continuing with /%02d", _map.size(), 32-n, 32-n-1);
        }

        ++n;
        _mask = IPFlowID(IPAddress::make_prefix(32 - n), _mask.sport(), IPAddress::make_prefix(32 - n), _mask.dport());

        for (auto flow : _map) {
            // Wildcards are intentionally excluded
            if ((flow.flowid().saddr().s() == "0.0.0.0") ||
                (flow.flowid().daddr().s() == "0.0.0.0")) {
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
        if (_pattern_type == IPFILTER) {
            acc << "allow ";
        }

        acc << "src net " << flow.flowid().saddr() << '/' << String(32-n) << " && "
            << "dst net " << flow.flowid().daddr() << '/' << String(32-n);
        if (_keep_sport)
            acc << " && src port " << flow.flowid().sport();
        if (_keep_dport) {
            acc << " && dst port " << flow.flowid().dport();
        }
        acc << ",\n";
    }

    return acc.take_string();
}

String
GenerateIPFilter::read_handler(Element *e, void *user_data)
{
    GenerateIPFilter *g = static_cast<GenerateIPFilter *>(e);
    if (!g) {
        return "GenerateIPFilter element not found";
    }

    assert(g->_pattern_type == IPFILTER || g->_pattern_type == IPCLASSIFIER);

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
GenerateIPFilter::add_handlers()
{
    add_read_handler("dump", read_handler, h_dump);
    add_read_handler("rules_nb", read_handler, h_rules_nb);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(GenerateIPFilter)
