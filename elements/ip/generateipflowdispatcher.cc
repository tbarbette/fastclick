// -*- c-basic-offset: 4; related-file-name: "generateipflowdispatcher.hh" -*-
/*
 * generateipflowdispatcher.{cc,hh} -- element generates flow rule patterns
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
 * DPDK Flow rules' generator out of incoming traffic.
 */
GenerateIPFlowDispatcher::GenerateIPFlowDispatcher() :
        _port(0), _queues_nb(DEF_NB_QUEUES),
        GenerateIPFilter(FLOW_DISPATCHER)
{
    _keep_dport = false;
}

GenerateIPFlowDispatcher::~GenerateIPFlowDispatcher()
{
}

int
GenerateIPFlowDispatcher::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String policy = "LOAD_AWARE";
    QueueAllocPolicy queue_alloc_policy = LOAD_AWARE;

    if (Args(conf, this, errh)
            .read_mp("PORT", _port)
            .read_p("NB_QUEUES", _queues_nb)
            .read("POLICY", policy)
            .consume() < 0)
        return -1;

    if (GenerateIPFilter::configure(conf, errh) < 0) {
        return -1;
    }

    int status = build_mask(_mask, _keep_saddr, _keep_daddr, _keep_sport, _keep_dport, _prefix);
    if (status != 0) {
        return errh->error("Cannot continue with empty mask");
    }

    if (_queues_nb == 0) {
        errh->error("NB_QUEUES must be a positive integer");
        return -1;
    }

    if ((policy.upper() == "ROUND_ROBIN") || (policy.upper() == "ROUND-ROBIN")) {
        queue_alloc_policy = ROUND_ROBIN;
    } else if ((policy.upper() == "LOAD_AWARE") || (policy.upper() == "LOAD-AWARE")) {
        queue_alloc_policy = LOAD_AWARE;
    } else {
        errh->error("Invalid POLICY. Select in [ROUND_ROBIN, LOAD_AWARE]");
        return -1;
    }

    errh->message("Queue allocation policy is set to: %s", policy.upper().c_str());

    // Create the supported DPDK flow rule formatter
    _rule_formatter_map.insert(
        static_cast<uint8_t>(RULE_DPDK),
        new DPDKFlowRuleFormatter(_port, _queues_nb, queue_alloc_policy, _keep_sport, _keep_dport));

    return 0;
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

IPFlowID
GenerateIPFlowDispatcher::get_mask(int prefix)
{
    IPFlowID fid = IPFlowID(IPAddress::make_prefix(prefix), _mask.sport(), IPAddress::make_prefix(prefix), _mask.dport());
    return fid;
}

bool
GenerateIPFlowDispatcher::is_wildcard(const IPFlow &flow)
{
    if ((flow.flowid().saddr().s() == "0.0.0.0") ||
        (flow.flowid().daddr().s() == "0.0.0.0")) {
        return true;
    }

    return false;
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
DPDKFlowRuleFormatter::policy_based_rule_generation(GenerateIPPacket::IPFlow &flow, const uint32_t flow_nb, const uint8_t prefix)
{
    // Wildcards are intentionally excluded
    if ((flow.flowid().saddr().s() == "0.0.0.0") &&
        (flow.flowid().daddr().s() == "0.0.0.0")) {
        return "";
    }

    StringAccum acc;
    acc << "ingress pattern eth /";

    bool with_ipv4 = false;
    if (flow.flowid().saddr().s() != "0.0.0.0") {
        acc << " ipv4 src spec ";
        acc << flow.flowid().saddr().unparse();
        acc << " src mask ";
        acc << IPAddress::make_prefix(prefix).unparse();
        with_ipv4 = true;
    }

    if (flow.flowid().daddr().s() != "0.0.0.0") {
        if (!with_ipv4)
            acc << " ipv4";
        acc << " dst spec ";
        acc << flow.flowid().daddr().unparse();
        acc << " dst mask ";
        acc << IPAddress::make_prefix(prefix).unparse();
    }
    acc << " /";

    // Incorporate transport layer ports if asked
    if (flow.flow_proto() != 0) {
        String proto_str = flow.flow_proto() == 6 ? "tcp" : "udp";

        if (_with_tp_s_port) {
            acc << " " << proto_str << " src is ";
            acc << flow.flowid().sport_host_order();
        }
        if (_with_tp_d_port) {
            acc << " " << proto_str << " dst is ";
            acc << flow.flowid().dport_host_order();
        }
        if ((_with_tp_s_port) || (_with_tp_d_port))
            acc << " / ";
    }

    // Select a NIC queue according to the input policy
    uint16_t chosen_queue = -1;
    switch (_queue_load->_queue_alloc_policy) {
        case ROUND_ROBIN:
           chosen_queue = round_robin(flow_nb, _queues_nb);
           break;
        case LOAD_AWARE:
           chosen_queue = find_less_loaded_queue(_queue_load->_queue_load_map);
           break;
        default:
            return "";
    }
    assert((chosen_queue >= 0) && (chosen_queue < _queues_nb));
    acc << " end actions queue index ";
    acc << chosen_queue;
    acc << " / count / end\n";

    // Update the queue load map
    uint64_t current_load = _queue_load->_queue_load_map[chosen_queue];
    uint64_t additional_load = flow.flow_size();
    _queue_load->_queue_load_map.insert(chosen_queue, current_load + additional_load);

    return acc.take_string();
}

String
QueueLoad::dump_stats()
{
    uint64_t total_load = 0;
    for (uint16_t i = 0; i < _queue_load_map.size(); i++) {
        total_load += _queue_load_map[i];
    }

    StringAccum acc;
    double balance_point_per_queue = (double) total_load / (double) _queues_nb;
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
    avg_total_load_imbalance_ratio /= _queues_nb;
    assert(avg_total_load_imbalance_ratio >= 0);
    _avg_total_load_imbalance_ratio = avg_total_load_imbalance_ratio;

    acc << "Average load imbalance ratio: ";
    acc.snprintf(8, "%.4f", avg_total_load_imbalance_ratio);
    acc << "%\n";

    return acc.take_string();
}

String
QueueLoad::dump_load()
{
    StringAccum acc;

    for (uint16_t i = 0; i < _queue_load_map.size(); i++) {
        acc << "NIC queue ";
        acc.snprintf(2, "%02d", i);
        acc << " load: ";
        acc.snprintf(15, "%15" PRIu64, _queue_load_map[i]);
        acc << " bytes\n";
    }

    return acc.take_string();
}

String
GenerateIPFlowDispatcher::read_handler(Element *e, void *user_data)
{
    GenerateIPFlowDispatcher *g = static_cast<GenerateIPFlowDispatcher *>(e);
    if (!g) {
        return "GenerateIPFlowDispatcher element not found";
    }
    assert(g->_pattern_type == FLOW_DISPATCHER);

    DPDKFlowRuleFormatter *rule_f = static_cast<DPDKFlowRuleFormatter *>(
        _rule_formatter_map[static_cast<uint8_t>(RULE_DPDK)]);
    assert(rule_f);

    intptr_t what = reinterpret_cast<intptr_t>(user_data);

    switch (what) {
        case h_flows_nb: {
            return String(g->_flows_nb);
        }
        case h_rules_nb: {
            return String(g->count_rules());
        }
        case h_dump: {
            return g->dump_rules(RULE_DPDK, true);
        }
        case h_load: {
            return rule_f->get_queue_load()->dump_load();
        }
        case h_stats: {
            return rule_f->get_queue_load()->dump_stats();
        }
        case h_avg_imbalance_ratio: {
            if (rule_f->get_queue_load()->imbalance_not_computed()) {
                rule_f->get_queue_load()->dump_stats();
            }
            return String(rule_f->get_queue_load()->get_avg_load_imbalance());
        }
        default: {
            click_chatter("Unknown read handler: %d", what);
            return "";
        }
    }
}

String
GenerateIPFlowDispatcher::to_file_handler(Element *e, void *user_data)
{
    GenerateIPFlowDispatcher *g = static_cast<GenerateIPFlowDispatcher *>(e);
    if (!g) {
        return "GenerateIPFlowDispatcher element not found";
    }

    String rules = g->dump_rules(RULE_DPDK, true);
    if (rules.empty()) {
        click_chatter("No rules to write to file: %s", g->_out_file.c_str());
        return "";
    }

    if (g->dump_rules_to_file(rules) != 0) {
        return "";
    }

    return "";
}

int
GenerateIPFlowDispatcher::param_handler(
        int operation, String &input, Element *e,
        const Handler *handler, ErrorHandler *errh) {
    GenerateIPFlowDispatcher *g = static_cast<GenerateIPFlowDispatcher *>(e);
    if (!g) {
        return errh->error("GenerateIPFlowDispatcher element not found");
    }

    DPDKFlowRuleFormatter *rule_f = static_cast<DPDKFlowRuleFormatter *>(
        _rule_formatter_map[static_cast<uint8_t>(RULE_DPDK)]);
    assert(rule_f);

    switch ((intptr_t)handler->read_user_data()) {
        case h_queue_imbalance_ratio: {
            if (input == "") {
                return errh->error("Parameter handler 'queue_imbalance_ratio' requires a queue index");
            }

            // Force to compute the ratios if not already computed
            if (rule_f->get_queue_load()->imbalance_not_computed()) {
                rule_f->get_queue_load()->dump_stats();
            }

            const uint16_t queue_id = atoi(input.c_str());
            input = String(rule_f->get_queue_load()->get_load_imbalance_of_queue(queue_id));

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
    add_read_handler("flows_nb", read_handler, h_flows_nb);
    add_read_handler("rules_nb", read_handler, h_rules_nb);
    add_read_handler("dump",  read_handler, h_dump);
    add_read_handler("dump_to_file", to_file_handler, h_dump_to_file);
    add_read_handler("load",  read_handler, h_load);
    add_read_handler("stats", read_handler, h_stats);
    add_read_handler("avg_imbalance_ratio", read_handler, h_avg_imbalance_ratio);

    set_handler("queue_imbalance_ratio", Handler::f_read | Handler::f_read_param, param_handler, h_queue_imbalance_ratio);
}

String
DPDKFlowRuleFormatter::flow_to_string(GenerateIPPacket::IPFlow &flow, const uint32_t flow_nb, const uint8_t prefix)
{
    assert((prefix > 0) && (prefix <= 32));

    String rule = policy_based_rule_generation(flow, flow_nb, prefix);
    if (rule.empty()) {
        return "";
    }

    return rule;
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel GenerateIPFilter)
EXPORT_ELEMENT(GenerateIPFlowDispatcher)
