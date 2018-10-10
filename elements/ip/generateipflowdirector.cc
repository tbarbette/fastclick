// -*- c-basic-offset: 4; related-file-name: "generateipflowdirector.hh" -*-
/*
 * GenerateIPFlowDirector.{cc,hh} -- element generates Flow Director patterns
 * out of input traffic, following DPDK's flow API syntax.
 * Georgios Katsikas, Tom Barbette
 *
 * Copyright (c) 2017 Georgios Katsikas, RISE SICS
 * Copyright (c) 2017 Tom Barbette, University of Liège
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

#include <cmath>

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/straccum.hh>

#include "generateipflowdirector.hh"

CLICK_DECLS

static const uint16_t DEF_NB_QUEUES = 16;

/**
 * Flow Director rules' generator out of incoming traffic.
 */
GenerateIPFlowDirector::GenerateIPFlowDirector() :
        _port(0), _nb_queues(DEF_NB_QUEUES),
        _queue_load_map(),
        _queue_alloc_policy(LOAD_AWARE),
        _queue_load_imbalance(),
        _avg_total_load_imbalance_ratio(NO_LOAD),
        GenerateIPFilter(FLOW_DIRECTOR)
{
    _keep_dport = false;
}

GenerateIPFlowDirector::~GenerateIPFlowDirector()
{
    if (!_queue_load_map.empty()) {
        _queue_load_map.clear();
    }

    if (!_queue_load_imbalance.empty()) {
        _queue_load_imbalance.clear();
    }
}

void
GenerateIPFlowDirector::init_queue_load_map(uint16_t queues_nb)
{
    for (uint16_t i = 0; i < queues_nb; i++) {
        _queue_load_map.insert(i, 0);
        _queue_load_imbalance.insert(i, NO_LOAD);
    }
}

int
GenerateIPFlowDirector::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String policy = "LOAD_AWARE";

    if (Args(conf, this, errh)
            .read_mp("PORT", _port)
            .read_p("NB_QUEUES", _nb_queues)
            .read("POLICY", policy)
            .read("PREFIX", _prefix)
            .consume() < 0)
        return -1;

    if (_nb_queues == 0) {
        errh->error("NB_QUEUES must be a positive integer");
        return -1;
    }
    // Initialize the load per NIC queue
    init_queue_load_map(_nb_queues);

    if (policy.upper() == "ROUND_ROBIN") {
        _queue_alloc_policy = ROUND_ROBIN;
    } else if (policy.upper() == "LOAD_AWARE") {
        _queue_alloc_policy = LOAD_AWARE;
    } else {
        errh->error("Invalid POLICY. Select in [ROUND_ROBIN, LOAD_AWARE]");
        return -1;
    }

    _mask = build_mask(_keep_sport, _keep_dport, _prefix);

    return GenerateIPFilter::configure(conf, errh);
}

int
GenerateIPFlowDirector::initialize(ErrorHandler *errh)
{
    return GenerateIPFilter::initialize(errh);
}

void
GenerateIPFlowDirector::cleanup(CleanupStage)
{
}

/**
 * Assign rules to NIC queues in a round-robin fashion.
 * This means, each new flow goes to the next NIC queue
 * and NIC queues are organized in a 'circular buffer'.
 */
static uint16_t
round_robin(const uint32_t current_flow_id, const uint16_t queues_nb)
{
    return (current_flow_id % queues_nb);
}

/**
 * Assign a new flow to the NIC queue that exhibits the least load.
 */
static uint16_t
find_less_loaded_queue(const HashMap<uint16_t, uint64_t> queue_load_map)
{
    uint64_t lowest_load = UINT64_MAX;
    uint16_t less_loaded_queue = 0;
    for (uint16_t i = 0; i < queue_load_map.size(); i++) {
        if (queue_load_map[i] <= lowest_load) {
            lowest_load = queue_load_map[i];
            less_loaded_queue = i;
        }
    }

    return less_loaded_queue;
}

static void
print_queue_load_map(const HashMap<uint16_t, uint64_t> queue_load_map)
{
    for (uint16_t i = 0; i < queue_load_map.size(); i++) {
        click_chatter("NIC queue %02d has load: %15" PRIu64, i, queue_load_map[i]);
    }
}

String
GenerateIPFlowDirector::policy_based_rule_generation(
        GenerateIPFlowDirector *g, const uint8_t aggregation_prefix)
{
    if (!g) {
        return "";
    }

    StringAccum acc;

    uint32_t i = 0;
    for (auto flow : g->_map) {
        // Wildcards are intentionally excluded
        if ((flow.flowid().saddr().s() == "0.0.0.0") ||
            (flow.flowid().daddr().s() == "0.0.0.0")) {
            continue;
        }

        acc << "ingress pattern eth /";
        acc << " ipv4 src spec ";
        acc << flow.flowid().saddr().unparse();
        acc << " src mask ";
        acc << IPAddress::make_prefix(32 - aggregation_prefix).unparse();
        acc << " dst spec ";
        acc << flow.flowid().daddr().unparse();
        acc << " dst mask ";
        acc << IPAddress::make_prefix(32 - aggregation_prefix).unparse().c_str();
        acc << " /";

        /**
         * Incorporate transport layer ports if asked
         */
        if (flow.flow_proto() != 0) {
            String proto_str = flow.flow_proto() == 6 ? "tcp" : "udp";

            if (g->_keep_sport) {
                acc << " " << proto_str << " src is ";
                acc << flow.flowid().sport();
            }
            if (g->_keep_dport) {
                acc << " " << proto_str << " dst is ";
                acc << flow.flowid().dport();
            }
            if ((g->_keep_sport) || (g->_keep_dport))
                acc << " / ";
        }


        // Select a NIC queue according to the input policy
        uint16_t chosen_queue = -1;
        switch (g->_queue_alloc_policy) {
            case ROUND_ROBIN:
               chosen_queue = round_robin(i, g->_nb_queues);
               break;
            case LOAD_AWARE:
               chosen_queue = find_less_loaded_queue(g->_queue_load_map);
               break;
            default:
                return "";
        }
        assert((chosen_queue >= 0) && (chosen_queue < g->_nb_queues));
        acc << " end actions queue index ";
        acc << chosen_queue;
        acc << " / count / end\n";

        // Update the queue load map
        uint64_t current_load = g->_queue_load_map[chosen_queue];
        uint64_t additional_load = flow.flow_size();
        g->_queue_load_map.insert(chosen_queue, current_load + additional_load);

        i++;
    }

    return acc.take_string();
}

String
GenerateIPFlowDirector::dump_stats(GenerateIPFlowDirector *g)
{
    if (!g) {
        return "";
    }

    uint64_t total_load = 0;
    for (uint16_t i = 0; i < g->_queue_load_map.size(); i++) {
        total_load += g->_queue_load_map[i];
    }

    StringAccum acc;
    double balance_point_per_queue = (double) total_load / (double) g->_nb_queues;
    double avg_total_load_imbalance_ratio = 0;

    if (balance_point_per_queue == 0) {
        g->_avg_total_load_imbalance_ratio = 0;
        click_chatter("No load in the system!");

        acc << "Average load imbalance ratio: " << avg_total_load_imbalance_ratio << "\n";
        return acc.take_string();
    }

    acc << "Ideal load per queue: ";
    acc.snprintf(15, "%.0f", balance_point_per_queue);
    acc << " bytes \n";

    for (uint16_t i = 0; i < g->_queue_load_map.size(); i++) {
        // This is the distance from the ideal load (assuming optimal load balancing)
        double load_distance_from_ideal = std::abs((double) g->_queue_load_map[i] - (double) balance_point_per_queue);
        // Normalize this distance
        double load_imbalance_ratio = (double) load_distance_from_ideal / (double) balance_point_per_queue;
        // Make percentage
        load_imbalance_ratio *= 100;
        // Imbalance ratio can be > 100, if load balancing is skewed
        assert(load_imbalance_ratio >= 0);

        // This is the load imbalance ratio of this queue
        g->_queue_load_imbalance.insert(i, load_imbalance_ratio);

        // Update the average imbalance ratio
        avg_total_load_imbalance_ratio += load_imbalance_ratio;

        acc << "NIC queue ";
        acc.snprintf(2, "%2d", i);
        acc << " distance from ideal load: ";
        acc.snprintf(15, "%15.0f", load_distance_from_ideal);
        acc << " bytes (load imbalance ratio ";
        acc.snprintf(9, "%8.4f", load_imbalance_ratio);
        acc << "%)\n";
    }

    // Average imbalance ratio
    avg_total_load_imbalance_ratio /= g->_nb_queues;
    assert(avg_total_load_imbalance_ratio >= 0);
    g->_avg_total_load_imbalance_ratio = avg_total_load_imbalance_ratio;

    acc << "Average load imbalance ratio: ";
    acc.snprintf(8, "%.4f", avg_total_load_imbalance_ratio);
    acc << "%\n";

    return acc.take_string();
}

String
GenerateIPFlowDirector::dump_load(GenerateIPFlowDirector *g)
{
    if (!g) {
        return "";
    }

    StringAccum acc;

    for (uint16_t i = 0; i < g->_queue_load_map.size(); i++) {
        acc << "NIC queue ";
        acc.snprintf(2, "%02d", i);
        acc << " load: ";
        acc.snprintf(15, "%15" PRIu64, g->_queue_load_map[i]);
        acc << " bytes\n";
    }

    acc << "\n";
    acc << "Total number of flows: " << g->_map.size() << "\n";

    return acc.take_string();
}

String
GenerateIPFlowDirector::dump_rules(GenerateIPFlowDirector *g, bool verbose)
{
    if (!g) {
        return "GenerateIPFlowDirector element not found";
    }
    assert(g->_pattern_type == FLOW_DIRECTOR);

    Timestamp before = Timestamp::now();

    uint8_t n = 32 - g->_prefix;
    while (g->_map.size() > g->_nrules) {
        if (verbose) {
            click_chatter("%8d rules with prefix /%02d, continuing with /%02d",g->_map.size(), 32-n, 32-n-1);
        }

        HashTable<IPFlow> new_map;
        ++n;
        g->_mask = IPFlowID(
            IPAddress::make_prefix(32 - n), g->_mask.sport(),
            IPAddress::make_prefix(32 - n), g->_mask.dport()
        );

        uint64_t i = 0;
        for (auto flow : g->_map) {
            // Check if we already have such a flow
            flow.set_mask(g->_mask);
            IPFlow *found = new_map.find(flow.flowid()).get();

            // New flow
            if (!found) {
                // Insert this new flow into the flow map
                new_map.find_insert(flow);
            } else {
                // Aggregate
                found->update_flow_size(flow.flow_size());
            }

            i++;
        }

        g->_map = new_map;
        if (n == 32) {
            return "Impossible to reduce the number of rules below: " + String(g->_map.size());
        }
    }

    Timestamp after = Timestamp::now();
    uint32_t elapsed_sec = (after - before).sec();

    if (verbose) {
        click_chatter("\n");
        click_chatter(
            "Time to create the flow map: %" PRIu32 " seconds (%.4f minutes)",
            elapsed_sec, (float) elapsed_sec / (float) 60
        );
    }

    return policy_based_rule_generation(g, n);
}

String
GenerateIPFlowDirector::read_handler(Element *e, void *user_data)
{
    GenerateIPFlowDirector *g = static_cast<GenerateIPFlowDirector *>(e);
    if (!g) {
        return "GenerateIPFlowDirector element not found";
    }
    intptr_t what = reinterpret_cast<intptr_t>(user_data);

    switch (what) {
        case h_dump: {
            return dump_rules(g, true);
        }
        case h_load: {
            return dump_load(g);
        }
        case h_stats: {
            return dump_stats(g);
        }
        case h_rules_nb: {
            if (g->_map.size() == 0) {
                dump_rules(g);
            }
            return String(g->_map.size());
        }
        case h_avg_imbalance_ratio: {
            if (g->_avg_total_load_imbalance_ratio == -1) {
                dump_stats(g);
            }
            return String(g->_avg_total_load_imbalance_ratio);
        }
        default: {
            click_chatter("Unknown read handler: %d", what);
            return "";
        }
    }
}

int
GenerateIPFlowDirector::param_handler(
        int operation, String &input, Element *e,
        const Handler *handler, ErrorHandler *errh) {
    GenerateIPFlowDirector *g = static_cast<GenerateIPFlowDirector *>(e);
    if (!g) {
        return errh->error("GenerateIPFlowDirector element not found");
    }

    switch ((intptr_t)handler->read_user_data()) {
        case h_queue_imbalance_ratio: {
            if (input == "") {
                return errh->error("Parameter handler 'queue_imbalance_ratio' requires a queue index");
            }

            const uint16_t queue_id = atoi(input.c_str());
            if (queue_id < g->_queue_load_imbalance.size()) {
                input = String(g->_queue_load_imbalance[queue_id]);
            } else {
                input = "0";
            }

            return 0;
        }
        default:
            return errh->error("Invalid operation in parameter handler");
    }

    return -1;
}

void
GenerateIPFlowDirector::add_handlers()
{
    add_read_handler("dump",  read_handler, h_dump);
    add_read_handler("load",  read_handler, h_load);
    add_read_handler("stats", read_handler, h_stats);
    add_read_handler("rules_nb", read_handler, h_rules_nb);
    add_read_handler("avg_imbalance_ratio", read_handler, h_avg_imbalance_ratio);

    set_handler("queue_imbalance_ratio", Handler::f_read | Handler::f_read_param, param_handler, h_queue_imbalance_ratio);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel GenerateIPFilter)
EXPORT_ELEMENT(GenerateIPFlowDirector)
