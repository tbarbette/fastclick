// -*- c-basic-offset: 4; related-file-name: "generateipflowdispatcher.hh" -*-
/*
 * GenerateIPFlowDispatcher.{cc,hh} -- element generates Flow Dispatcher patterns
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

#include "generateipflowdispatcher.hh"

CLICK_DECLS

static const uint16_t DEF_NB_QUEUES = 16;

/**
 * Flow Dispatcher rules' generator out of incoming traffic.
 */
GenerateIPFlowDispatcher::GenerateIPFlowDispatcher() :
        _port(0), _nb_queues(DEF_NB_QUEUES),
        _queue_load_map(),
        _queue_alloc_policy(LOAD_AWARE),
        _queue_load_imbalance(),
        _avg_total_load_imbalance_ratio(NO_LOAD),
        GenerateIPFilter(FLOW_DISPATCHER)
{
    _keep_dport = false;
}

GenerateIPFlowDispatcher::~GenerateIPFlowDispatcher()
{
    if (!_queue_load_map.empty()) {
        _queue_load_map.clear();
    }

    if (!_queue_load_imbalance.empty()) {
        _queue_load_imbalance.clear();
    }
}

void
GenerateIPFlowDispatcher::init_queue_load_map(uint16_t queues_nb)
{
    for (uint16_t i = 0; i < queues_nb; i++) {
        _queue_load_map.insert(i, 0);
        _queue_load_imbalance.insert(i, NO_LOAD);
    }
}

int
GenerateIPFlowDispatcher::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String policy = "LOAD_AWARE";

    if (Args(conf, this, errh)
            .read_mp("PORT", _port)
            .read_p("NB_QUEUES", _nb_queues)
            .read("POLICY", policy)
            .consume() < 0)
        return -1;

    int status = build_mask(_mask, _keep_saddr, _keep_daddr, _keep_sport, _keep_dport, _prefix);
    if (status != 0) {
        return errh->error("Cannot continue with empty mask");
    }

    if (_nb_queues == 0) {
        errh->error("NB_QUEUES must be a positive integer");
        return -1;
    }
    // Initialize the load per NIC queue
    init_queue_load_map(_nb_queues);

    if ((policy.upper() == "ROUND_ROBIN") || (policy.upper() == "ROUND-ROBIN")) {
        _queue_alloc_policy = ROUND_ROBIN;
    } else if ((policy.upper() == "LOAD_AWARE") || (policy.upper() == "LOAD-AWARE")) {
        _queue_alloc_policy = LOAD_AWARE;
    } else {
        errh->error("Invalid POLICY. Select in [ROUND_ROBIN, LOAD_AWARE]");
        return -1;
    }

    return GenerateIPFilter::configure(conf, errh);
}

int
GenerateIPFlowDispatcher::initialize(ErrorHandler *errh)
{
    return GenerateIPFilter::initialize(errh);
}

void
GenerateIPFlowDispatcher::cleanup(CleanupStage)
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
print_queue_load_map(const HashMap<uint16_t, uint64_t> &queue_load_map)
{
    for (uint16_t i = 0; i < queue_load_map.size(); i++) {
        click_chatter("NIC queue %02d has load: %15" PRIu64, i, queue_load_map[i]);
    }
}

String
GenerateIPFlowDispatcher::policy_based_rule_generation(const uint8_t aggregation_prefix)
{
    StringAccum acc;

    uint32_t i = 0;
    for (auto flow : _map) {
        // Wildcards are intentionally excluded
        if ((flow.flowid().saddr().s() == "0.0.0.0") &&
            (flow.flowid().daddr().s() == "0.0.0.0")) {
            continue;
        }

        acc << "ingress pattern eth /";

        bool with_ipv4 = false;
        if (flow.flowid().saddr().s() != "0.0.0.0") {
            acc << " ipv4 src spec ";
            acc << flow.flowid().saddr().unparse();
            acc << " src mask ";
            acc << IPAddress::make_prefix(32 - aggregation_prefix).unparse();
            with_ipv4 = true;
        }

        if (flow.flowid().daddr().s() != "0.0.0.0") {
            if (!with_ipv4)
                acc << " ipv4";
            acc << " dst spec ";
            acc << flow.flowid().daddr().unparse();
            acc << " dst mask ";
            acc << IPAddress::make_prefix(32 - aggregation_prefix).unparse();
        }
        acc << " /";

        // Incorporate transport layer ports if asked
        if (flow.flow_proto() != 0) {
            String proto_str = flow.flow_proto() == 6 ? "tcp" : "udp";

            if (_keep_sport) {
                acc << " " << proto_str << " src is ";
                acc << flow.flowid().sport();
            }
            if (_keep_dport) {
                acc << " " << proto_str << " dst is ";
                acc << flow.flowid().dport();
            }
            if ((_keep_sport) || (_keep_dport))
                acc << " / ";
        }


        // Select a NIC queue according to the input policy
        uint16_t chosen_queue = -1;
        switch (_queue_alloc_policy) {
            case ROUND_ROBIN:
               chosen_queue = round_robin(i, _nb_queues);
               break;
            case LOAD_AWARE:
               chosen_queue = find_less_loaded_queue(_queue_load_map);
               break;
            default:
                return "";
        }
        assert((chosen_queue >= 0) && (chosen_queue < _nb_queues));
        acc << " end actions queue index ";
        acc << chosen_queue;
        acc << " / count / end\n";

        // Update the queue load map
        uint64_t current_load = _queue_load_map[chosen_queue];
        uint64_t additional_load = flow.flow_size();
        _queue_load_map.insert(chosen_queue, current_load + additional_load);

        i++;
    }

    return acc.take_string();
}

String
GenerateIPFlowDispatcher::dump_stats()
{
    uint64_t total_load = 0;
    for (uint16_t i = 0; i < _queue_load_map.size(); i++) {
        total_load += _queue_load_map[i];
    }

    StringAccum acc;
    double balance_point_per_queue = (double) total_load / (double) _nb_queues;
    double avg_total_load_imbalance_ratio = 0;

    if (balance_point_per_queue == 0) {
        _avg_total_load_imbalance_ratio = 0;
        click_chatter("No load in the system!");

        acc << "Average load imbalance ratio: " << avg_total_load_imbalance_ratio << "\n";
        return acc.take_string();
    }

    acc << "Ideal load per queue: ";
    acc.snprintf(15, "%.0f", balance_point_per_queue);
    acc << " bytes \n";

    for (uint16_t i = 0; i < _queue_load_map.size(); i++) {
        // This is the distance from the ideal load (assuming optimal load balancing)
        double load_distance_from_ideal = (double) _queue_load_map[i] - (double) balance_point_per_queue;
        // Normalize this distance
        double load_imbalance_ratio = (double) load_distance_from_ideal / (double) balance_point_per_queue;
        // Make percentage
        load_imbalance_ratio *= 100;

        // This is the load imbalance ratio of this queue
        _queue_load_imbalance.insert(i, load_imbalance_ratio);

        // Update the average imbalance ratio
        avg_total_load_imbalance_ratio += std::abs(load_imbalance_ratio);

        acc << "NIC queue ";
        acc.snprintf(2, "%2d", i);
        acc << " distance from ideal load: ";
        acc.snprintf(15, "%15.0f", load_distance_from_ideal);
        acc << " bytes (load imbalance ratio ";
        acc.snprintf(9, "%8.4f", load_imbalance_ratio);
        acc << "%)\n";
    }

    // Average imbalance ratio
    avg_total_load_imbalance_ratio /= _nb_queues;
    assert(avg_total_load_imbalance_ratio >= 0);
    _avg_total_load_imbalance_ratio = avg_total_load_imbalance_ratio;

    acc << "Average load imbalance ratio: ";
    acc.snprintf(8, "%.4f", avg_total_load_imbalance_ratio);
    acc << "%\n";

    return acc.take_string();
}

String
GenerateIPFlowDispatcher::dump_load()
{
    StringAccum acc;

    for (uint16_t i = 0; i < _queue_load_map.size(); i++) {
        acc << "NIC queue ";
        acc.snprintf(2, "%02d", i);
        acc << " load: ";
        acc.snprintf(15, "%15" PRIu64, _queue_load_map[i]);
        acc << " bytes\n";
    }

    acc << "\n";
    acc << "Total number of flows: " << _map.size() << "\n";

    return acc.take_string();
}

String
GenerateIPFlowDispatcher::dump_rules(bool verbose)
{
    assert(_pattern_type == FLOW_DISPATCHER);

    Timestamp before = Timestamp::now();

    uint8_t n = 32 - _prefix;
    while (_map.size() > _nrules) {
        if (verbose) {
            click_chatter("%8d rules with prefix /%02d, continuing with /%02d", _map.size(), 32-n, 32-n-1);
        }

        HashTable<IPFlow> new_map;
        ++n;
        _mask = IPFlowID(IPAddress::make_prefix(32 - n), _mask.sport(), IPAddress::make_prefix(32 - n), _mask.dport());

        uint64_t i = 0;
        for (auto flow : _map) {
            // Check if we already have such a flow
            flow.set_mask(_mask);
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

        _map = new_map;
        if (n == 32) {
            return "Impossible to reduce the number of rules below: " + String(_map.size());
        }
    }

    if (verbose) {
        click_chatter("%8d rules with prefix /%02d", _map.size(), 32-n);
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

    return policy_based_rule_generation(n);
}

String
GenerateIPFlowDispatcher::read_handler(Element *e, void *user_data)
{
    GenerateIPFlowDispatcher *g = static_cast<GenerateIPFlowDispatcher *>(e);
    if (!g) {
        return "GenerateIPFlowDispatcher element not found";
    }
    intptr_t what = reinterpret_cast<intptr_t>(user_data);

    switch (what) {
        case h_dump: {
            return g->dump_rules(true);
        }
        case h_load: {
            return g->dump_load();
        }
        case h_stats: {
            return g->dump_stats();
        }
        case h_rules_nb: {
            if (g->_map.size() == 0) {
                g->dump_rules();
            }
            return String(g->_map.size());
        }
        case h_avg_imbalance_ratio: {
            if (g->_avg_total_load_imbalance_ratio == -1) {
                g->dump_stats();
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
GenerateIPFlowDispatcher::param_handler(
        int operation, String &input, Element *e,
        const Handler *handler, ErrorHandler *errh) {
    GenerateIPFlowDispatcher *g = static_cast<GenerateIPFlowDispatcher *>(e);
    if (!g) {
        return errh->error("GenerateIPFlowDispatcher element not found");
    }

    switch ((intptr_t)handler->read_user_data()) {
        case h_queue_imbalance_ratio: {
            if (input == "") {
                return errh->error("Parameter handler 'queue_imbalance_ratio' requires a queue index");
            }

            // Force to compute the ratios if not already computed
            if (g->_avg_total_load_imbalance_ratio == -1) {
                g->dump_stats();
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
GenerateIPFlowDispatcher::add_handlers()
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
EXPORT_ELEMENT(GenerateIPFlowDispatcher)
