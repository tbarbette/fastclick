// -*- c-basic-offset: 4; related-file-name: "flowdirector.hh" -*-
/*
 * flowdirector.cc -- library for integrating DPDK's Flow Director in Click
 *
 * Copyright (c) 2018 Georgios Katsikas, RISE SICS
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
#include <click/straccum.hh>
#include <click/flowdirector.hh>

CLICK_DECLS

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)

#include <rte_flow.h>

/**
 * Flow Director implementation.
 */

// DPDKDevice mode is Flow Director
String FlowDirector::FLOW_DIR_MODE   = "flow_dir";

// Supported flow director handlers (called from FromDPDKDevice)
String FlowDirector::FLOW_RULE_ADD   = "rule_add";
String FlowDirector::FLOW_RULE_DEL   = "rule_del";
String FlowDirector::FLOW_RULE_STATS = "rule_stats";
String FlowDirector::FLOW_RULE_LIST  = "rules_list";
String FlowDirector::FLOW_RULE_COUNT = "rules_count";
String FlowDirector::FLOW_RULE_FLUSH = "rules_flush";

// Set of flow rule items supported by the Flow API
HashMap<int, String> FlowDirector::_flow_item;

// Set of flow rule actions supported by the Flow API
HashMap<int, String> FlowDirector::_flow_action;

// Flow rule counter per device
HashTable<portid_t, uint32_t> FlowDirector::_rules_nb;

// Next rule ID per device
HashTable<portid_t, uint32_t> FlowDirector::_next_rule_id;

// Matched packets and bytes per rule ID per device
HashTable<portid_t, HashTable<uint32_t, uint64_t>> FlowDirector::_matched_pkts;
HashTable<portid_t, HashTable<uint32_t, uint64_t>> FlowDirector::_matched_bytes;

// Default verbosity setting
bool FlowDirector::DEF_VERBOSITY = false;

// Global table of DPDK ports mapped to their Flow Director objects
HashTable<portid_t, FlowDirector *> FlowDirector::_dev_flow_dir;

// A unique parser
struct cmdline *FlowDirector::_parser = NULL;

// Error handling
int FlowDirector::ERROR = -1;
int FlowDirector::SUCCESS = 0;

FlowDirector::FlowDirector() :
        _port_id(-1), _active(false),
        _verbose(DEF_VERBOSITY), _rules_filename("")
{
    _errh = new ErrorVeneer(ErrorHandler::default_handler());
}

FlowDirector::FlowDirector(portid_t port_id, ErrorHandler *errh) :
        _port_id(port_id), _active(false),
        _verbose(DEF_VERBOSITY), _rules_filename("")
{
    _errh = new ErrorVeneer(errh);
    _rules_nb[_port_id] = 0;
    _next_rule_id[_port_id] = 0;

    populate_supported_flow_items_and_actions();

    if (verbose()) {
        _errh->message(
            "Flow Director (port %u): Created (state %s)",
            _port_id, _active ? "active" : "inactive"
        );
    }
}

FlowDirector::~FlowDirector()
{
    // Destroy the parser
    if (_parser) {
        cmdline_quit(_parser);
        delete _parser;
        _parser = NULL;
        if (verbose()) {
            _errh->message(
                "Flow Director (port %u): Parser deleted", _port_id
            );
        }
    }

    // Clean up flow rule counters
    if (!_rules_nb.empty()) {
        _rules_nb.clear();
    }

    if (!_next_rule_id.empty()) {
        _next_rule_id.clear();
    }

    _flow_item.clear();
    _flow_action.clear();

    if (verbose()) {
        _errh->message(
            "Flow Director (port %u): Destroyed", _port_id
        );
    }

    delete_error_handler();
}

void
FlowDirector::populate_supported_flow_items_and_actions()
{
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_END, "END");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_VOID, "VOID");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_INVERT, "INVERT");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_VF, "VF");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_PHY_PORT, "PHY_PORT");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_PORT_ID, "PORT_ID");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_RAW, "RAW");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_ETH, "ETH");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_VLAN, "VLAN");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_IPV4, "IPV4");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_IPV6, "IPV6");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_ICMP, "ICMP");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_UDP, "UDP");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_TCP, "TCP");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_VXLAN, "VXLAN");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_E_TAG, "E_TAG");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_NVGRE, "NVGRE");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_MPLS, "MPLS");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_GRE, "GRE");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_FUZZY, "FUZZY");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_GTP, "GTP");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_GTPC, "GTPC");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_GTPU, "GTPU");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_GENEVE, "GENEVE");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_VXLAN_GPE, "VXLAN_GPE");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_ARP_ETH_IPV4, "ARP_ETH_IPV4");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_IPV6_EXT, "IPV6_EXT");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_ICMP6, "ICMP6");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_ICMP6_ND_NS, "ICMP6_ND_NS");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_ICMP6_ND_NA, "ICMP6_ND_NA");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_ICMP6_ND_OPT, "ICMP6_ND_OPT");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_ICMP6_ND_OPT_SLA_ETH, "ICMP6_ND_OPT_SLA_ETH");
    _flow_item.insert((int) RTE_FLOW_ITEM_TYPE_ICMP6_ND_OPT_TLA_ETH, "ICMP6_ND_OPT_TLA_ETH");

    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_END, "END");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_VOID, "VOID");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_PASSTHRU, "PASSTHRU");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_MARK, "MARK");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_FLAG, "FLAG");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_QUEUE, "QUEUE");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_DROP, "DROP");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_COUNT, "COUNT");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_RSS, "RSS");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_PF, "PF");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_VF, "VF");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_PHY_PORT, "PHY_PORT");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_PORT_ID, "PORT_ID");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_METER, "METER");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_SET_MPLS_TTL, "OF_SET_MPLS_TTL");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_DEC_MPLS_TTL, "OF_DEC_MPLS_TTL");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_SET_NW_TTL, "OF_SET_NW_TTL");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_DEC_NW_TTL, "OF_DEC_NW_TTL");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_COPY_TTL_OUT, "OF_COPY_TTL_OUT");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_COPY_TTL_IN, "OF_COPY_TTL_IN");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_POP_VLAN, "OF_POP_VLAN");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_PUSH_VLAN, "OF_PUSH_VLAN");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_SET_VLAN_VID, "OF_SET_VLAN_VID");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_SET_VLAN_PCP, "OF_SET_VLAN_PCP");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_POP_MPLS, "OF_POP_MPLS");
    _flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_PUSH_MPLS, "OF_PUSH_MPLS");
}

/**
 * Obtains an instance of the Flow Director parser.
 *
 * @param errh an instance of the error handler
 * @return a Flow Director parser object
 */
struct cmdline *
FlowDirector::parser(ErrorHandler *errh)
{
    if (!_parser) {
        return flow_parser_init(errh);
    }

    return _parser;
}

/**
 * Returns the global map of DPDK ports to
 * their Flow Director instances.
 *
 * @return a Flow Director instance map
 */
HashTable<portid_t, FlowDirector *>
FlowDirector::flow_director_map()
{
    return _dev_flow_dir;
}

/**
 * Cleans the global map of DPDK ports to
 * their Flow Director instances.
 */
void
FlowDirector::clean_flow_director_map()
{
    if (!_dev_flow_dir.empty()) {
        _dev_flow_dir.clear();
    }
}

/**
 * Manages the Flow Director instances.
 *
 * @param port_id the ID of the NIC
 * @param errh an instance of the error handler
 * @return a Flow Director object for this NIC
 */
FlowDirector *
FlowDirector::get_flow_director(
        const portid_t &port_id,
        ErrorHandler   *errh)
{
    if (!errh) {
        errh = ErrorHandler::default_handler();
    }

    // Invalid port ID
    if (port_id >= DPDKDevice::dev_count()) {
        click_chatter(
            "Flow Director (port %u): Denied to create instance for invalid port",
            port_id
        );
        return NULL;
    }

    // Get the Flow Director of the desired port
    FlowDirector *flow_dir = _dev_flow_dir.get(port_id);

    // Not there, let's created it
    if (!flow_dir) {
        flow_dir = new FlowDirector(port_id, errh);
        assert(flow_dir);
        _dev_flow_dir[port_id] = flow_dir;
    }

    // Create a Flow Director parser
    _parser = parser(errh);

    // Ship it back
    return flow_dir;
}

/**
 * Installs a set of string-based rules read from a file.
 * Allowed rule type is only a `create` rule.
 *
 * @param filename the file that contains the rules
 */
int
FlowDirector::add_rules_from_file(const String &filename)
{
    uint32_t rules_nb = 0;
    uint32_t installed_rules_nb = flow_rules_count();

    FILE *fp = NULL;
    fp = fopen(filename.c_str(), "r");
    if (fp == NULL) {
        return _errh->error(
            "Flow Director (port %u): Failed to open file '%s'",
            _port_id, filename.c_str());
    }

    char *line = NULL;
    size_t len = 0;
    const char ignore_chars[] = "\n\t ";

    // Read file line-by-line (or rule-by-rule)
    while ((getline(&line, &len, fp)) != -1) {
        rules_nb++;

        // Skip empty lines or lines with only spaces/tabs
        if (!line || (strlen(line) == 0) ||
            (strchr(ignore_chars, line[0]))) {
            _errh->warning("Flow Director (port %u): Invalid rule #%" PRIu32, _port_id, rules_nb);
            continue;
        }

        // Detect and remove unwanted components
        if (!filter_rule(&line)) {
            _errh->warning(
                "Flow Director (port %u): Invalid rule #%" PRIu32 ": %s", _port_id, rules_nb, line
            );
            continue;
        }

        // Compose rule
        String rule = "flow create " + String(_port_id) + " " + String(line);

        if (_verbose) {
            _errh->message("[NIC %u] About to install rule #%" PRIu32 ": %s",
                _port_id, rules_nb, rule.c_str()
            );
        }

        if (flow_rule_install(installed_rules_nb, rule) == SUCCESS) {
            installed_rules_nb++;
        }
    }

    // Close the file
    fclose(fp);

    _errh->message(
        "Flow Director (port %u): %" PRIu32 "/%" PRIu32 " rules are installed",
        _port_id, flow_rules_count(), rules_nb
    );

    return SUCCESS;
}

/**
 * Translates a string-based rule into a flow rule
 * object and installs it in a NIC.
 *
 * @param rule_id a flow rule's ID
 * @param rule a flow rule as a string
 * @return a flow rule object
 */
int
FlowDirector::flow_rule_install(const uint32_t &rule_id, const String rule)
{
    // Only active instances can configure a NIC
    if (!active()) {
        _errh->error(
            "Flow Director (port %u): Inactive instance cannot install rule #%" PRIu32,
            _port_id, rule_id
        );
        return ERROR;
    }

    // TODO: Fix DPDK to return proper status
    int res = flow_parser_parse(_parser, (char *) rule.c_str(), _errh);
    if (res >= 0) {
        _rules_nb[_port_id]++;
        _next_rule_id[_port_id]++;
        // Initialize flow rule statistics
        _matched_pkts[_port_id][rule_id] = 0;
        _matched_bytes[_port_id][rule_id] = 0;
        return SUCCESS;
    }

    // Resolve the error
    String error;
    switch (res) {
        case CMDLINE_PARSE_BAD_ARGS:
            error = "bad arguments";
            break;
        case CMDLINE_PARSE_AMBIGUOUS:
            error = "ambiguous input";
            break;
        case CMDLINE_PARSE_NOMATCH:
            error = "no match";
            break;
        default:
            error = "unknown error";
            break;
    }

    _errh->error(
        "Flow Director (port %u): Failed to parse rule #%" PRIu32 " due to %s",
        _port_id, rule_id, error.c_str()
    );

    return ERROR;
}

/**
 * Returns a flow rule object of a specific port with a specific ID.
 *
 * @param rule_id a rule ID
 * @return a flow rule object
 */
struct port_flow *
FlowDirector::flow_rule_get(const uint32_t &rule_id)
{
    struct rte_port *port = get_port(_port_id);
    if (!port->flow_list) {
        return NULL;
    }

    for (struct port_flow *pf = port->flow_list; pf != NULL; pf = pf->next) {
        if (pf->id == rule_id) {
            return pf;
        }
    }

    return NULL;
}

/**
 * Removes a flow rule object from the NIC.
 *
 * @param rule_id a flow rule's ID
 * @return status
 */
int
FlowDirector::flow_rule_delete(const uint32_t &rule_id)
{
    // Only active instances can configure a NIC
    if (!active()) {
        return ERROR;
    }

    const uint32_t rules_to_delete[] = {rule_id};

    if (port_flow_destroy(_port_id, 1, rules_to_delete) == FLOWDIR_SUCCESS) {
        _rules_nb[_port_id]--;
        _matched_pkts[_port_id].erase(rule_id);
        _matched_bytes[_port_id].erase(rule_id);
        return SUCCESS;
    }

    return ERROR;
}

/**
 * Queries the statistics of a NIC flow rule.
 *
 * @param rule_id a flow rule's ID
 * @return flow rule statistics as a string
 */
String
FlowDirector::flow_rule_query(const uint32_t &rule_id)
{
    // Only active instances can query a NIC
    if (!active()) {
        return "";
    }

    struct rte_flow_error error;
    struct rte_port *port;
    struct port_flow *pf;
    struct rte_flow_action *action = 0;
    struct rte_flow_query_count query;

    port = get_port(_port_id);
    if (!port->flow_list || (flow_rules_count() == 0)) {
        _errh->message("Flow Director (port %u): No flow rules to query", _port_id);
        return "";
    }

    // Find the desired flow rule
    for (pf = port->flow_list; pf; pf = pf->next) {
        if (pf->id == rule_id) {
            action = pf->actions;
            break;
        }
    }
    if (!pf || !action) {
        _errh->message(
            "Flow Director (port %u): No stats for invalid flow rule #%" PRIu32,
            _port_id, rule_id
        );
        return "";
    }

    // Move the action pointer at the right offset
    while (action->type != RTE_FLOW_ACTION_TYPE_END) {
        if (action->type == RTE_FLOW_ACTION_TYPE_COUNT) {
            break;
        }
        ++action;
    }
    if (action->type != RTE_FLOW_ACTION_TYPE_COUNT) {
        _errh->message(
            "Flow Director (port %u): No count instruction for flow rule #%" PRIu32,
            _port_id, rule_id
        );
        return "";
    }

    // Poisoning to make sure PMDs update it in case of error
    memset(&error, 0x55, sizeof(error));
    memset(&query, 0, sizeof(query));

    if (rte_flow_query(_port_id, pf->flow, action, &query, &error) < 0) {
        _errh->message(
            "Flow Director (port %u): Failed to query stats for flow rule #%" PRIu32,
            _port_id, rule_id
        );
        return "";
    }

    if (query.hits_set == 1) {
        _matched_pkts[_port_id][rule_id] = query.hits;
    }
    if (query.bytes_set == 1) {
        _matched_bytes[_port_id][rule_id] = query.bytes;
    }

    StringAccum stats;

    stats << "hits_set: " << query.hits_set << ", "
          << "bytes_set: " << query.bytes_set << ", "
          << "hits: " << query.hits << ", "
          << "bytes: " << query.bytes;

    return stats.take_string();
}

/**
 * Reports a flow rule's packet counter.
 *
 * @param rule_id a flow rule's ID
 * @return reference to a flow's packet counter
 */
int64_t
FlowDirector::flow_rule_pkt_stats(const uint32_t &rule_id)
{
    // Prevent access to invalid indices
    if (_matched_pkts[_port_id].find(rule_id) != _matched_pkts[_port_id].end()) {
        return (int64_t) _matched_pkts[_port_id][rule_id];
    }

    return (int64_t) -1;
}

/**
 * Reports a flow rule's byte counter.
 *
 * @param rule_id a flow rule's ID
 * @return reference to a flow's byte counter
 */
int64_t
FlowDirector::flow_rule_byte_stats(const uint32_t &rule_id)
{
    // Prevent access to invalid indices
    if (_matched_bytes[_port_id].find(rule_id) != _matched_bytes[_port_id].end()) {
        return (int64_t) _matched_bytes[_port_id][rule_id];
    }

    return (int64_t) -1;
}

/**
 * Reports aggregate flow rule statistics on a port.
 *
 * @return aggregate flow rule statistics as a string
 */
String
FlowDirector::flow_rule_aggregate_stats()
{
    // Only active instances might have statistics
    if (!active()) {
        return "";
    }

    struct rte_port *port = get_port(_port_id);
    if (!port->flow_list || (flow_rules_count() == 0)) {
        _errh->warning("Flow Director (port %u): No aggregate statistics due to no traffic", _port_id);
        return "";
    }

    uint64_t tot_pkts = 0;
    uint64_t tot_bytes = 0;
    HashTable<uint16_t, uint64_t> pkts_per_queue;
    HashTable<uint16_t, uint64_t> bytes_per_queue;

    // Traverse the list of installed flow rules
    for (struct port_flow *pf = port->flow_list; pf != NULL; pf = pf->next) {
        const struct rte_flow_action *action = pf->actions;

        int queue = -1;
        while (action->type != RTE_FLOW_ACTION_TYPE_END) {
            if (action->type == RTE_FLOW_ACTION_TYPE_QUEUE) {
                queue = *(uint16_t *) action->conf;
                break;
            }
            ++action;
        }

        // This rule does not have a relevant dispatching action
        if (queue < 0) {
            continue;
        }

        // Initialize the counters for this queue
        if (pkts_per_queue.find((uint16_t) queue) != pkts_per_queue.end()) {
            pkts_per_queue[(uint16_t) queue] = 0;
            bytes_per_queue[(uint16_t) queue] = 0;
        }

        // Count packets and bytes per queue as well as across queues
        pkts_per_queue[(uint16_t) queue] += _matched_pkts[_port_id][pf->id];
        bytes_per_queue[(uint16_t) queue] += _matched_bytes[_port_id][pf->id];
        tot_pkts += _matched_pkts[_port_id][pf->id];
        tot_bytes += _matched_bytes[_port_id][pf->id];
    }

    uint16_t queues_nb = pkts_per_queue.size();
    if (queues_nb == 0) {
        _errh->warning("Flow Director (port %u): No queues to produce aggregate statistics", _port_id);
        return "";
    }

    float perfect_pkts_per_queue = (float) tot_pkts / (float) queues_nb;
    float perfect_bytes_per_queue = (float) tot_bytes / (float) queues_nb;

    float pkt_imbalance_ratio = 0.0;
    float byte_imbalance_ratio = 0.0;
    for (uint16_t i = 0; i < queues_nb; i++) {
        if (perfect_pkts_per_queue != 0) {
            pkt_imbalance_ratio += (float) (abs(pkts_per_queue[i] - perfect_pkts_per_queue)) /
                                           (float) perfect_pkts_per_queue;
        }
        if (perfect_bytes_per_queue != 0) {
            byte_imbalance_ratio += (float) (abs(bytes_per_queue[i] - perfect_bytes_per_queue)) /
                                            (float) perfect_bytes_per_queue;
        }
    }

    StringAccum aggr_stats;
    aggr_stats << "Packet imbalance ratio over " << queues_nb << " queues: " << pkt_imbalance_ratio << "\n";
    aggr_stats << "Bytes imbalance ratio over " << queues_nb << " queues: " << byte_imbalance_ratio << "";

    return aggr_stats.take_string();
}

/**
 * Return the explicit rule counter for a particular NIC.
 *
 * @return the number of rules being installed
 */
uint32_t
FlowDirector::flow_rules_count()
{
    return _rules_nb[_port_id];
}

/**
 * Counts all of the rules installed in a NIC
 * by traversing the list of rules.
 *
 * @return the number of rules being installed
 */
uint32_t
FlowDirector::flow_rules_count_explicit()
{
    // Only active instances might have some rules
    if (!active()) {
        return 0;
    }

    struct rte_port *port = get_port(_port_id);
    if (!port->flow_list) {
        if (verbose()) {
            _errh->message("Flow Director (port %u): No flows", _port_id);
        }
        return 0;
    }

    uint32_t rules_nb = 0;

    // Traverse the list of installed flow rules
    for (struct port_flow *pf = port->flow_list; pf != NULL; pf = pf->next) {
        rules_nb++;
    }

    // Consistency
    assert(_rules_nb[_port_id] == rules_nb);

    return rules_nb;
}

/**
 * Lists all of a NIC's rules.
 *
 * @return a string of comma separated NIC flow rules
 */
String
FlowDirector::flow_rules_list()
{
    if (!active()) {
        return 0;
    }

    struct rte_port *port = get_port(_port_id);
    if (!port->flow_list || (flow_rules_count() == 0)) {
        _errh->error("Flow Director (port %u): No flow rules to list", _port_id);
        return "No flow rules";
    }

    struct port_flow *pf;
    struct port_flow *list = NULL;

    // Sort flows by group, priority, and ID.
    for (pf = port->flow_list; pf != NULL; pf = pf->next) {
        struct port_flow **tmp;

        tmp = &list;
        while (*tmp &&
               (pf->attr.group > (*tmp)->attr.group ||
            (pf->attr.group == (*tmp)->attr.group &&
             pf->attr.priority > (*tmp)->attr.priority) ||
            (pf->attr.group == (*tmp)->attr.group &&
             pf->attr.priority == (*tmp)->attr.priority &&
             pf->id > (*tmp)->id)))
            tmp = &(*tmp)->tmp;
        pf->tmp = *tmp;
        *tmp = pf;
    }

    StringAccum rules_list;

    // Traverse and print the list of installed flow rules
    for (pf = list; pf != NULL; pf = pf->tmp) {
        const struct rte_flow_item *item = pf->pattern;
        const struct rte_flow_action *action = pf->actions;

        rules_list << "Flow rule #" << pf->id << ": [";
        rules_list << "Group: " << pf->attr.group << ", Prio: " << pf->attr.priority << ", ";
        rules_list << "Scope: " << (pf->attr.ingress == 1 ? "ingress" : "-");
        rules_list << "/" << (pf->attr.egress == 1 ? "egress" : "-");
        rules_list << "/" << (pf->attr.transfer == 1 ? "transfer" : "-");
        rules_list << ", ";
        rules_list << "Matches:";

        while (item->type != RTE_FLOW_ITEM_TYPE_END) {
            if (item->type != RTE_FLOW_ITEM_TYPE_VOID) {
                rules_list << " ";
                rules_list << _flow_item[item->type];
            }
            ++item;
        }

        rules_list << " => ";
        rules_list << "Actions:";

        while (action->type != RTE_FLOW_ACTION_TYPE_END) {
            if (action->type != RTE_FLOW_ACTION_TYPE_VOID) {
                rules_list << " ";
                rules_list << _flow_action[action->type];
            }

            ++action;
        }

        // There is a valid index for flow rule counters
        if (_matched_pkts[_port_id].find(pf->id) != _matched_pkts[_port_id].end()) {
            rules_list << ", ";
            rules_list << "Stats: ";
            rules_list << "Matched packets: " << _matched_pkts[_port_id][pf->id] << ", ";
            rules_list << "Matched bytes: " << _matched_bytes[_port_id][pf->id];
        }

        rules_list << "]\n";
    }

    if (rules_list.empty()) {
        rules_list << "No flow rules";
    }

    return rules_list.take_string();
}

/**
 * Flushes all of the rules from a NIC associated with this
 * Flow Director instance.
 *
 * @return the number of rules being flushed
 */
uint32_t
FlowDirector::flow_rules_flush()
{
    // Only active instances can configure a NIC
    if (!active()) {
        if (verbose()) {
            _errh->message("Flow Director (port %u): Nothing to flush", _port_id);
        }
        return 0;
    }

    uint32_t rules_before_flush = flow_rules_count_explicit();
    if (rules_before_flush == 0) {
        flush_rule_counters_on_port();
        return 0;
    }

    // Successful flush means zero rules left
    if (port_flow_flush(_port_id) == FLOWDIR_SUCCESS) {
        _rules_nb[_port_id] = 0;
        flush_rule_counters_on_port();
        return rules_before_flush;
    }

    // Now, count again to verify what is left
    return flow_rules_count_explicit();
}

/**
 * Filters unwanted components from rule and
 * returns an updated rule by reference.
 *
 */
bool
FlowDirector::filter_rule(char **rule)
{
    assert(rule);

    const char *prefix = "flow create";
    size_t prefix_len = strlen(prefix);
    size_t rule_len = strlen(*rule);

    // Fishy
    if (rule_len <= prefix_len) {
        return false;
    }

    // Rule starts with prefix
    if (strncmp(prefix, *rule, prefix_len) == 0) {
        // Skip the prefix
        *rule += prefix_len;

        // Remove a potential port ID and spaces before the actual rule
        while (true) {
            if (!isdigit(**rule) && (**rule != ' ')) {
                break;
            }
            (*rule)++;
        }

        return (strlen(*rule) > 0) ? true : false;
    }

    return true;
}

bool
FlowDirector::flush_rule_counters_on_port()
{
    _matched_pkts[_port_id].clear();
    _matched_bytes[_port_id].clear();
}

#endif /* RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0) */

CLICK_ENDDECLS
