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

static const bool VERBOSE = false;
static const uint16_t DEF_NB_QUEUES = 16;

/**
 * Flow Director rules' generator out of incoming traffic.
 */
GenerateIPFlowDirector::GenerateIPFlowDirector() :
        _port(0), _nb_queues(DEF_NB_QUEUES),
        _queue_load_map(),
        _queue_alloc_policy(LOAD_AWARE),
        GenerateIPFilter()
{
    _keep_dport = false;
}

GenerateIPFlowDirector::~GenerateIPFlowDirector()
{
}

void
GenerateIPFlowDirector::init_queue_load_map(uint16_t queues_nb)
{
    for (uint16_t i = 0; i < queues_nb; i++) {
        _queue_load_map.insert(i, 0);
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

    // Default mask
    _mask = IPFlowID(0xffffffff, (_keep_sport ? 0xffff:0), 0xffffffff, (_keep_dport ? 0xffff:0));

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

Packet *
GenerateIPFlowDirector::simple_action(Packet *p)
{
    return GenerateIPFilter::simple_action(p);
}

#if HAVE_BATCH
PacketBatch*
GenerateIPFlowDirector::simple_action_batch(PacketBatch *batch)
{
    return GenerateIPFilter::simple_action_batch(batch);
}
#endif

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
        click_chatter("NIC queue %2d has load: %15" PRIu64, i, queue_load_map[i]);
    }
}

String
GenerateIPFlowDirector::policy_based_rule_generation(
        GenerateIPFlowDirector *g, const uint8_t aggregation_prefix, Timestamp elapsed)
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

        acc << "flow create "<< String(g->_port);
        acc << " ingress pattern eth /";
        acc << " ipv4 src spec ";
        acc.snprintf(15, "%15s", flow.flowid().saddr().unparse().c_str());
        acc << " src mask ";
        acc.snprintf(15, "%15s", IPAddress::make_prefix(32 - aggregation_prefix).unparse().c_str());
        acc << " dst spec ";
        acc.snprintf(15, "%15s", flow.flowid().daddr().unparse().c_str());
        acc << " dst mask ";
        acc.snprintf(15, "%15s", IPAddress::make_prefix(32 - aggregation_prefix).unparse().c_str());
        acc << " /";

        /**
         * Incorporate transport layer ports if asked
         */
        if (flow.flow_proto() != 0) {
            String proto_str = flow.flow_proto() == 6 ? "tcp" : "udp";

            if (g->_keep_sport) {
                acc << " " << proto_str << " src is ";
                acc.snprintf(5, "%5d", flow.flowid().sport());
            }
            if (g->_keep_dport) {
                acc << " " << proto_str << " dst is ";
                acc.snprintf(5, "%5d", flow.flowid().dport());
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
        acc.snprintf(2, "%2d", chosen_queue);
        acc << " / end\n";

        // Update the queue load map
        uint64_t current_load = g->_queue_load_map[chosen_queue];
        uint64_t additional_load = flow.flow_size();
        g->_queue_load_map.insert(chosen_queue, current_load + additional_load);

        i++;
    }

    uint32_t elapsed_sec = elapsed.sec();

    acc << "\n";
    acc << "Time to create the flow map: ";
    acc.snprintf(9, "%" PRIu32, elapsed_sec);
    acc << " seconds (";
    acc.snprintf(5, "%.2f", (float) elapsed_sec / (float) 60);
    acc << " minutes)\n";

    return acc.take_string();
}

String
GenerateIPFlowDirector::dump_stats(GenerateIPFlowDirector *g)
{
    if (!g) {
        return "";
    }

    StringAccum acc;

    uint64_t total_load = 0;
    for (uint16_t i = 0; i < g->_queue_load_map.size(); i++) {
        total_load += g->_queue_load_map[i];
    }

    double balance_point_per_queue = (double) total_load / (double) g->_nb_queues;
    acc << "Ideal load per queue: ";
    acc.snprintf(15, "%.0f", balance_point_per_queue);
    acc << " bytes \n";

    double total_load_imbalance_ratio = 0;

    for (uint16_t i = 0; i < g->_queue_load_map.size(); i++) {
        // This is the distance from the ideal load (assuming optimal load balancing)
        double load_distance_from_ideal = std::abs((double) g->_queue_load_map[i] - (double) balance_point_per_queue);
        // Normalize this distance
        double load_imbalance_ratio = (double) load_distance_from_ideal / (double) balance_point_per_queue;
        // Imbalance ratio can be > 1, if load balancing is skewed
        assert(load_imbalance_ratio >= 0);

        // Update the total imbalance ratio
        total_load_imbalance_ratio += load_imbalance_ratio;

        acc << "NIC queue ";
        acc.snprintf(2, "%2d", i);
        acc << " distance from ideal load: ";
        acc.snprintf(15, "%15.0f", load_distance_from_ideal);
        acc << " bytes (load imbalance ratio ";
        acc.snprintf(9, "%8.4f", load_imbalance_ratio * 100);
        acc << "%)\n";
    }
    // Average imbalance ratio
    total_load_imbalance_ratio /= g->_nb_queues;
    // Total imbalance ratio can be > 1, if load balancing is skewed
    assert(total_load_imbalance_ratio >= 0);

    acc << "Total load imbalance ratio: ";
    acc.snprintf(8, "%.4f", total_load_imbalance_ratio * 100);
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
        acc.snprintf(2, "%2d", i);
        acc << " load: ";
        acc.snprintf(15, "%15" PRIu64, g->_queue_load_map[i]);
        acc << " bytes\n";
    }

    acc << "\n";
    acc << "Total number of flows: " << g->_map.size() << "\n";

    return acc.take_string();
}

String
GenerateIPFlowDirector::dump_rules(GenerateIPFlowDirector *g)
{
    if (!g) {
        return "GenerateIPFlowDirector element not found";
    }

    Timestamp before = Timestamp::now();

    uint8_t n = 0;
    while (g->_map.size() > g->_nrules) {
        HashTable<IPFlow> new_map;
        ++n;
        g->_mask = IPFlowID(
            IPAddress::make_prefix(32 - n), g->_mask.sport(),
            IPAddress::make_prefix(32 - n), g->_mask.dport()
        );

        uint64_t i = 0;
        for (auto flow : g->_map) {

            // Duplicate this map
            HashTable<IPFlow> map_copy = g->_map;

            // Aggregate this flow by one bit
            uint32_t init_flow_size = flow.flow_size();
            flow.set_mask(g->_mask);
            flow.set_proto(0);
            flow.update_flow_size(0);

            uint64_t m = 0;
            // Go through the old flows and see how many of them match
            for (auto int_flow : map_copy) {
                bool src_match = int_flow.flowid().saddr().matches_prefix(
                    flow.flowid().saddr(), g->_mask.saddr()
                );
                bool dst_match = int_flow.flowid().daddr().matches_prefix(
                    flow.flowid().daddr(), g->_mask.daddr()
                );
                // This is optional
                bool proto_match = true;
                if ((g->_keep_sport) || (g->_keep_dport)) {
                    proto_match = int_flow.flow_proto() == flow.flow_proto();
                }

                // Matches the aggregate rule
                if (src_match && dst_match && proto_match) {
                    m++;
                    // This flow is now a little heavier
                    flow.update_flow_size(int_flow.flow_size());
                    flow.set_proto(int_flow.flow_proto());
                }
            }

            // Watch out, a lot prints!!
            if (VERBOSE) {
                click_chatter(
                    "New flow %5" PRIu64 ": %15s --> %15s matches %3" PRIu64 " flows. "
                    "Previous size %12" PRIu32 " -- New size %12" PRIu32,
                    i,
                    flow.flowid().saddr().s().c_str(),
                    flow.flowid().daddr().s().c_str(),
                    m,
                    init_flow_size,
                    flow.flow_size()
                );
            }

            new_map.find_insert(flow);

            i++;
        }

        g->_map = new_map;
        if (n == 32) {
            return "Impossible to reduce the number of rules below: " + String(g->_map.size());
        }
    }

    Timestamp after = Timestamp::now();

    return policy_based_rule_generation(g, n, after - before);
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
            return dump_rules(g);
        }
        case h_load: {
            return dump_load(g);
        }
        case h_stats: {
            return dump_stats(g);
        }
        default: {
            click_chatter("Unknown read handler: %d", what);
            return "";
        }
    }
}

void
GenerateIPFlowDirector::add_handlers()
{
    add_read_handler("dump",  read_handler, h_dump);
    add_read_handler("load",  read_handler, h_load);
    add_read_handler("stats", read_handler, h_stats);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel dpdk GenerateIPFilter)
EXPORT_ELEMENT(GenerateIPFlowDirector)
