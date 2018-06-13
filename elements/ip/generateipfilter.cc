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
#include "generateipflowdirector.hh"

CLICK_DECLS

const int GenerateIPPacket::DEF_NB_RULES = 8000;

/**
 * Base class for pattern generation out of incoming traffic.
 */
GenerateIPPacket::GenerateIPPacket() : _nrules(DEF_NB_RULES)
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

void
GenerateIPPacket::cleanup(CleanupStage)
{
    return;
}

/**
 * IP FIlter rules' generator out of incoming traffic.
 */
GenerateIPFilter::GenerateIPFilter() :
    GenerateIPPacket(), _keep_sport(false), _keep_dport(true),
    _pattern_type(NONE)
{
}

GenerateIPFilter::GenerateIPFilter(RulePattern pattern_type) :
    GenerateIPPacket(), _keep_sport(false), _keep_dport(true)
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
            .read("KEEP_SPORT", _keep_sport)
            .read("KEEP_DPORT", _keep_dport)
            .read("PATTERN_TYPE", pattern_type)
            .consume() < 0)
        return -1;

    _mask = IPFlowID(0xffffffff, (_keep_sport?0xffff:0), 0xffffffff, (_keep_dport?0xffff:0));

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
    // GenerateIPFlowDirector sub-class
    } else if (_pattern_type == FLOW_DIRECTOR) {
        GenerateIPFlowDirector *fd_ptr = dynamic_cast<GenerateIPFlowDirector *>(this);
        if (fd_ptr == NULL) {
            errh->error("Invalid PATTERN_TYPE for GenerateIPFlowDirector.");
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
GenerateIPFilter::read_handler(Element *e, void *user_data)
{
    GenerateIPFilter *g = static_cast<GenerateIPFilter *>(e);
    if (!g) {
        return "GenerateIPFilter element not found";
    }

    assert(g->_pattern_type == IPFILTER || g->_pattern_type == IPCLASSIFIER);

    uint8_t n = 0;
    while (g->_map.size() > g->_nrules) {
        HashTable<IPFlow> new_map;
        ++n;
        g->_mask = IPFlowID(
            IPAddress::make_prefix(32 - n), g->_mask.sport(),
            IPAddress::make_prefix(32 - n), g->_mask.dport()
        );
        for (auto flow : g->_map) {
            // Wildcards are intentionally excluded
            if ((flow.flowid().saddr().s() == "0.0.0.0") ||
                (flow.flowid().daddr().s() == "0.0.0.0")) {
                continue;
            }

            flow.set_mask(g->_mask);
            new_map.find_insert(flow);
        }
        g->_map = new_map;
        if (n == 32) {
            return "Impossible to reduce the number of rules below: " + String(g->_map.size());
        }
    }

    uint64_t rules_nb = 0;
    StringAccum acc;

    for (auto flow : g->_map) {
        if (g->_pattern_type == IPFILTER) {
            acc << "allow ";
        }

        acc << "src net " << flow.flowid().saddr() << '/' << String(32-n) << " && "
                 << " dst net " << flow.flowid().daddr() << '/' << String(32-n);
        if (g->_keep_sport)
            acc << " && src port " << flow.flowid().sport();
        if (g->_keep_dport) {
            acc << " && dst port " << flow.flowid().dport();
        }
        acc << ",\n";

        rules_nb++;
    }

    acc << "\n";
    acc << "# of Rules: ";
    acc.snprintf(12, "%" PRIu64, rules_nb);
    acc << "\n";

    return acc.take_string();
}

void
GenerateIPFilter::add_handlers()
{
    add_read_handler("dump", read_handler);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(GenerateIPFilter)
