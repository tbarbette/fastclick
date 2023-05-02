// -*- c-basic-offset: 4; related-file-name: "flowrulemanager.hh" -*-
/*
 * flowrulemanager.cc -- Flow rule manager implementation for DPDK-based NICs, based on DPDK's Flow API
 *
 * Copyright (c) 2018 Georgios Katsikas, RISE SICS & KTH Royal Institute of Technology
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

#include <limits>

#include <click/config.h>
#include <click/straccum.hh>
#include <click/flowrulemanager.hh>

CLICK_DECLS

#if RTE_VERSION >= RTE_VERSION_NUM(20,2,0,0)

#include <rte_flow.h>

/**
 * DPDK Flow Rule Manager implementation.
 */

// DPDKDevice mode for the Flow Rule Manager
String FlowRuleManager::DISPATCHING_MODE = "flow";

// Supported Flow Rule Manager handlers (called from FromDPDKDevice)
String FlowRuleManager::FLOW_RULE_ADD             = "rule_add";
String FlowRuleManager::FLOW_RULE_DEL             = "rules_del";
String FlowRuleManager::FLOW_RULE_IDS_GLB         = "rules_ids_global";
String FlowRuleManager::FLOW_RULE_IDS_INT         = "rules_ids_internal";
String FlowRuleManager::FLOW_RULE_PACKET_HITS     = "rule_packet_hits";
String FlowRuleManager::FLOW_RULE_BYTE_COUNT      = "rule_byte_count";
String FlowRuleManager::FLOW_RULE_AGGR_STATS      = "rules_aggr_stats";
String FlowRuleManager::FLOW_RULE_LIST            = "rules_list";
String FlowRuleManager::FLOW_RULE_LIST_WITH_HITS  = "rules_list_with_hits";
String FlowRuleManager::FLOW_RULE_COUNT           = "rules_count";
String FlowRuleManager::FLOW_RULE_COUNT_WITH_HITS = "rules_count_with_hits";
String FlowRuleManager::FLOW_RULE_ISOLATE         = "rules_isolate";
String FlowRuleManager::FLOW_RULE_FLUSH           = "rules_flush";

// Set of flow rule items supported by the Flow API
HashMap<int, String> FlowRuleManager::flow_item;

// Set of flow rule actions supported by the Flow API
HashMap<int, String> FlowRuleManager::flow_action;

// Default verbosity settings
bool FlowRuleManager::DEF_VERBOSITY = false;
bool FlowRuleManager::DEF_DEBUG_MODE = false;

// Global table of DPDK ports mapped to their Flow Rule Manager objects
HashTable<portid_t, FlowRuleManager *> FlowRuleManager::dev_flow_rule_mgr;

// Map of ports to their flow rule installation/deletion statistics
HashMap<portid_t, Vector<RuleTiming>> FlowRuleManager::_rule_inst_stats_map;
HashMap<portid_t, Vector<RuleTiming>> FlowRuleManager::_rule_del_stats_map;

// Isolation mode per port
HashMap<portid_t, bool> FlowRuleManager::_isolated;

// A unique parser
struct cmdline *FlowRuleManager::_parser = NULL;

FlowRuleManager::FlowRuleManager() :
        _port_id(-1), _active(false), _verbose(DEF_VERBOSITY), _debug_mode(DEF_DEBUG_MODE), _rules_filename("")
{
    _errh = new ErrorVeneer(ErrorHandler::default_handler());
    _flow_rule_cache = 0;
}

FlowRuleManager::FlowRuleManager(portid_t port_id, ErrorHandler *errh) :
        _port_id(port_id), _active(false), _verbose(DEF_VERBOSITY), _debug_mode(DEF_DEBUG_MODE), _rules_filename("")
{
    _errh = new ErrorVeneer(errh);
    _flow_rule_cache = new FlowRuleCache(port_id, _verbose, _debug_mode, _errh);

    populate_supported_flow_items_and_actions();

    if (verbose()) {
        _errh->message("DPDK Flow Rule Manager (port %u): Created (state %s)", _port_id, _active ? "active" : "inactive");
    }
}

FlowRuleManager::~FlowRuleManager()
{
    // Destroy the parser
    if (_parser) {
        cmdline_quit(_parser);
        delete _parser;
        _parser = NULL;
        if (verbose()) {
            _errh->message("DPDK Flow Rule Manager (port %u): Parser deleted", _port_id);
        }
    }

    if (_isolated.size() > 0) {
        _isolated.clear();
    }

    if (_flow_rule_cache) {
        delete _flow_rule_cache;
    }

    flow_item.clear();
    flow_action.clear();

    if (verbose()) {
        _errh->message("DPDK Flow Rule Manager (port %u): Destroyed", _port_id);
    }

    if (_rule_inst_stats_map.size() > 0) {
        _rule_inst_stats_map.clear();
    }

    if (_rule_del_stats_map.size() > 0) {
        _rule_del_stats_map.clear();
    }

    delete_error_handler();
}



#if RTE_VERSION >= RTE_VERSION_NUM(22,11,0,0)
#define RTE_FLOW_ITEM_TYPE_VF RTE_FLOW_ITEM_TYPE_REPRESENTED_PORT
#define RTE_FLOW_ITEM_TYPE_PHY_PORT RTE_FLOW_ITEM_TYPE_REPRESENTED_PORT
#define RTE_FLOW_ACTION_TYPE_PHY_PORT RTE_FLOW_ACTION_TYPE_REPRESENTED_PORT
#endif

void
FlowRuleManager::populate_supported_flow_items_and_actions()
{
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_END, "END");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_VOID, "VOID");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_INVERT, "INVERT");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_VF, "VF");
#if RTE_VERSION >= RTE_VERSION_NUM(18,5,0,0)
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_PHY_PORT, "PHY_PORT");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_PORT_ID, "PORT_ID");
#endif
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_RAW, "RAW");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_ETH, "ETH");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_VLAN, "VLAN");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_IPV4, "IPV4");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_IPV6, "IPV6");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_ICMP, "ICMP");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_UDP, "UDP");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_TCP, "TCP");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_VXLAN, "VXLAN");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_E_TAG, "E_TAG");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_NVGRE, "NVGRE");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_MPLS, "MPLS");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_GRE, "GRE");
#if RTE_VERSION >= RTE_VERSION_NUM(17,8,0,0)
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_FUZZY, "FUZZY");
#endif
#if RTE_VERSION >= RTE_VERSION_NUM(17,11,0,0)
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_GTP, "GTP");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_GTPC, "GTPC");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_GTPU, "GTPU");
#endif
#if RTE_VERSION >= RTE_VERSION_NUM(18,2,0,0)
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_GENEVE, "GENEVE");
#endif
#if RTE_VERSION >= RTE_VERSION_NUM(18,5,0,0)
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_VXLAN_GPE, "VXLAN_GPE");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_ARP_ETH_IPV4, "ARP_ETH_IPV4");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_IPV6_EXT, "IPV6_EXT");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_ICMP6, "ICMP6");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_ICMP6_ND_NS, "ICMP6_ND_NS");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_ICMP6_ND_NA, "ICMP6_ND_NA");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_ICMP6_ND_OPT, "ICMP6_ND_OPT");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_ICMP6_ND_OPT_SLA_ETH, "ICMP6_ND_OPT_SLA_ETH");
    flow_item.insert((int) RTE_FLOW_ITEM_TYPE_ICMP6_ND_OPT_TLA_ETH, "ICMP6_ND_OPT_TLA_ETH");
#endif

    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_END, "END");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_VOID, "VOID");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_PASSTHRU, "PASSTHRU");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_MARK, "MARK");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_FLAG, "FLAG");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_QUEUE, "QUEUE");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_DROP, "DROP");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_COUNT, "COUNT");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_RSS, "RSS");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_PF, "PF");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_VF, "VF");
#if RTE_VERSION >= RTE_VERSION_NUM(17,11,0,0)
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_METER, "METER");
#endif
#if RTE_VERSION >= RTE_VERSION_NUM(18,5,0,0)
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_PHY_PORT, "PHY_PORT");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_PORT_ID, "PORT_ID");

# if RTE_VERSION < RTE_VERSION_NUM(22,11,0,0)
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_SET_MPLS_TTL, "OF_SET_MPLS_TTL");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_DEC_MPLS_TTL, "OF_DEC_MPLS_TTL");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_SET_NW_TTL, "OF_SET_NW_TTL");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_DEC_NW_TTL, "OF_DEC_NW_TTL");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_COPY_TTL_OUT, "OF_COPY_TTL_OUT");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_COPY_TTL_IN, "OF_COPY_TTL_IN");
# endif
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_POP_VLAN, "OF_POP_VLAN");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_PUSH_VLAN, "OF_PUSH_VLAN");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_SET_VLAN_VID, "OF_SET_VLAN_VID");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_SET_VLAN_PCP, "OF_SET_VLAN_PCP");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_POP_MPLS, "OF_POP_MPLS");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_PUSH_MPLS, "OF_PUSH_MPLS");
#endif
}

/**
 * Obtains an instance of the Flow Rule Manager parser.
 *
 * @args errh: an instance of the error handler
 * @return Flow Rule Manager's parser object
 */
struct cmdline *
FlowRuleManager::flow_rule_parser(ErrorHandler *errh)
{
    if (!_parser) {
        return flow_rule_parser_init(errh);
    }

    return _parser;
}

/**
 * Obtains the flow cache associated with this Flow Rule Manager.
 *
 * @return a Flow Cache object
 */
FlowRuleCache *
FlowRuleManager::flow_rule_cache()
{
    return _flow_rule_cache;
}

/**
 * Returns the global map of DPDK ports to
 * their Flow Rule Manager instances.
 *
 * @return a Flow Rule Manager instance map
 */
HashTable<portid_t, FlowRuleManager *>
FlowRuleManager::flow_rule_manager_map()
{
    return dev_flow_rule_mgr;
}

/**
 * Cleans the global map of DPDK ports to
 * their Flow Rule Manager instances.
 */
void
FlowRuleManager::clean_flow_rule_manager_map()
{
    if (!dev_flow_rule_mgr.empty()) {
        dev_flow_rule_mgr.clear();
    }
}

/**
 * Manages the Flow Rule Manager instances.
 *
 * @args port_id: the ID of the NIC
 * @args errh: an instance of the error handler
 * @return a Flow Rule Manager object for this NIC
 */
FlowRuleManager *
FlowRuleManager::get_flow_rule_mgr(const portid_t &port_id, ErrorHandler *errh)
{
    if (!errh) {
        errh = ErrorHandler::default_handler();
        if(!errh)
            errh = new ErrorHandler();
    }

    // Invalid port ID
    if (port_id >= DPDKDevice::dev_count()) {
	    errh->error("DPDK Flow Rule Manager (port %u): Denied to create instance for invalid port", port_id);
        return NULL;
    }

    // Get the Flow Rule Manager of the desired port
    FlowRuleManager *flow_rule_mgr = dev_flow_rule_mgr.get(port_id);

    // Not there, let's created it
    if (!flow_rule_mgr) {
        flow_rule_mgr = new FlowRuleManager(port_id, errh);
        assert(flow_rule_mgr);
        dev_flow_rule_mgr[port_id] = flow_rule_mgr;
    }

    // Create Flow Rule Manager's parser
    _parser = flow_rule_parser(errh);

    // Ship it back
    return flow_rule_mgr;
}

/**
 * Calibrates the flow rule cache before new rule(s) are inserted.
 * Transforms the input array into a map and calls an overloaded flow_rule_cache_calibrate.
 *
 * @arg int_rule_ids: an array of internal flow rule IDs to be deleted
 * @arg rules_nb: the number of flow rules to be deleted
 */
void
FlowRuleManager::flow_rule_cache_calibrate(const uint32_t *int_rule_ids, const uint32_t &rules_nb)
{
    HashMap<uint32_t, String> rules_map;
    for (uint32_t i = 0; i < rules_nb ; i++) {
        uint32_t rule_id = _flow_rule_cache->global_from_internal_rule_id(int_rule_ids[i]);
        // We only need the rule IDs, not the actual rules
        rules_map.insert(rule_id, "");
    }

    flow_rule_cache_calibrate(rules_map);
}

/**
 * Calibrates the flow rule cache before new rule(s) are inserted.
 * This method helps to understand how DPDK's flow API allocates internal rule IDs.
 * Normally these IDs are ever increasing integers.
 * For example:
 *    Current list of flow rules: 0 1 2 3 4 5
 *    Event --> Delete rule with ID 2
 *    Event --> Insert rule (the new ID is 5 + 1)
 *    Current list of flow rules: 0 1 3 4 5 6
 * However, there is at least one case that the above example does not hold.
 * For example:
 *    Current list of flow rules: 0 1 2 3 4 5
 *    Event --> Delete rule with ID 5
 *    Event --> Insert rule (the new ID is 4 + 1)
 *    Current list of flow rules: 0 1 2 3 4 5
 * This is crazy but it's how DPDK works at the moment ;(
 *
 * @args rules_map: a map of global rule IDs to their values be inserted
 */
void
FlowRuleManager::flow_rule_cache_calibrate(const HashMap<uint32_t, String> &rules_map)
{
    bool calibrate = false;
    Vector<uint32_t> candidates;
    int32_t max_int_id = _flow_rule_cache->currently_max_internal_rule_id();

    // Now insert each rule in the flow cache
    auto it = rules_map.begin();
    while (it != rules_map.end()) {
        uint32_t rule_id = it.key();
        int32_t int_id = _flow_rule_cache->internal_from_global_rule_id(rule_id);
        if (int_id < 0) {
            it++;
            continue;
        }

        // One of the rules to update has the largest internal ID ->
        // We need to update the next internal rule ID based on that.
        if (int_id == max_int_id) {
            calibrate = true;
        }

        candidates.push_back(int_id);

        it++;
    }

    if (!calibrate) {
        return;
    }

    // Sort the list of candidates by decreasing order
    _flow_rule_cache->sort_rule_ids_dec(candidates);

    if (_debug_mode) {
        String c_str = "";
        for (auto c : candidates) {
            c_str += String(c) + " ";
        }
        _errh->message("Candidates: %s", c_str.c_str());
    }

    // Normally, the candidate is the largest internal ID (e.g., 43)
    int32_t the_candidate = max_int_id;

    uint32_t i = 0;
    for (auto c : candidates) {
        // Unless another ID is less than the largest by 1 (e.g., 42 then 41)
        if ((c + i) >= max_int_id) {
            the_candidate = c;
        }
        i++;
    }

    assert(the_candidate >= 0);

    // Calibrate the candidate ID according to what other IDs the cache contains
    _flow_rule_cache->correlate_candidate_id_with_cache(the_candidate);

    // Update the next internal rule ID
    _flow_rule_cache->set_next_internal_rule_id(the_candidate);
}

/**
 * Returns a set of string-based flow rules read from a file.
 *
 * @args filename: the file that contains the flow rules
 * @return a string of newline-separated flow rules in memory
 */
String
FlowRuleManager::flow_rules_from_file_to_string(const String &filename)
{
    String rules_str = "";

    if (filename.empty()) {
        _errh->warning("DPDK Flow Rule Manager (port %u): No file provided", _port_id);
        return rules_str;
    }

    FILE *fp = NULL;
    fp = fopen(filename.c_str(), "r");
    if (fp == NULL) {
        _errh->error("DPDK Flow Rule Manager (port %u): Failed to open file '%s'", _port_id, filename.c_str());
        return rules_str;
    }
    _errh->message("DPDK Flow Rule Manager (port %u): Opened file '%s'", _port_id, filename.c_str());

    uint32_t rules_nb = 0;
    uint32_t loaded_rules_nb = 0;

    char *line = NULL;
    size_t len = 0;
    const char ignore_chars[] = "\n\t ";

    // Read file line-by-line (or rule-by-rule)
    while ((getline(&line, &len, fp)) != -1) {
        // Skip empty lines or lines with only spaces/tabs
        if (!line || (strlen(line) == 0) ||
            (strchr(ignore_chars, line[0]))) {
            continue;
        }

        rules_nb++;

        // Detect and remove unwanted components
        String rule = String(line);
        if (!flow_rule_filter(rule)) {
            _errh->error("DPDK Flow Rule Manager (port %u): Invalid rule '%s'", _port_id, line);
            continue;
        }

        // Compose rule for the right NIC
        rule = "flow create " + String(_port_id) + " " + rule;

        // Append this rule to a string
        rules_str += rule;

        loaded_rules_nb++;
    }

    // Close the file
    fclose(fp);

    _errh->message("DPDK Flow Rule Manager (port %u): Loaded %" PRIu32 "/%" PRIu32 " rules", _port_id, loaded_rules_nb, rules_nb);

    return rules_str;
}

/**
 * Updates a set of flow rules from a map.
 *
 * @args rules_map: a map of global rule IDs to their rules
 * @args by_controller: boolean flag that denotes that rule installation is driven by a controller (defaults to true)
 * @return the number of flow rules being installed/updated, otherwise a negative integer
 */
int32_t
FlowRuleManager::flow_rules_update(const HashMap<uint32_t, String> &rules_map, bool by_controller, int core_id)
{
    uint32_t rules_to_install = rules_map.size();
    if (rules_to_install == 0) {
        return (int32_t) _errh->error("DPDK Flow Rule Manager (port %u): Failed to add rules due to empty input map", _port_id);
    }

    // Current capacity
    int32_t capacity = (int32_t) flow_rules_count();

    // Prepare the cache counter for new deletions and insertions
    flow_rule_cache_calibrate(rules_map);

    String rules_str = "";
    uint32_t installed_rules_nb = 0;

    // Initialize the counters for the new internal rule ID
    uint32_t *int_rule_ids = (uint32_t *) malloc(rules_to_install * sizeof(uint32_t));
    if (!int_rule_ids) {
        return (int32_t) _errh->error("DPDK Flow Rule Manager (port %u): Failed to allocate space to store %" PRIu32 " rule IDs", _port_id, rules_to_install);
    }

    Vector<uint32_t> glb_rule_ids_vec;
    Vector<uint32_t> int_rule_ids_vec;
    Vector<uint32_t> old_int_rule_ids_vec;

    // Now insert each rule in the flow cache
    auto it = rules_map.begin();
    while (it != rules_map.end()) {
        uint32_t rule_id = it.key();
        String rule = it.value();
        if (rule.empty()) {
            it++;
            continue;
        }

        // Parse the queue index to infer the CPU core
        if (core_id < 0) {
            String queue_index_str = fetch_token_after_keyword((char *) rule.c_str(), "queue index");
            core_id = atoi(queue_index_str.c_str());
        }

        // Fetch the old internal rule ID associated with this global rule ID
        int32_t old_int_rule_id = _flow_rule_cache->internal_from_global_rule_id(rule_id);

        // Get the right internal rule ID
        uint32_t int_rule_id = 0;
        if (by_controller) {
            int_rule_id = _flow_rule_cache->next_internal_rule_id();
        } else {
            int_rule_id = rule_id;
        }

        if (_verbose) {
            _errh->message(
                "DPDK Flow Rule Manager (port %u): About to install rule with global ID %" PRIu32 " and internal ID %" PRIu32 " on core %d: %s",
                _port_id, rule_id, int_rule_id, core_id, rule.c_str()
            );
        }

        // Update the flow cache
        if (!_flow_rule_cache->update_rule_in_flow_cache(core_id, rule_id, int_rule_id, rule)) {
            return FLOWRULEPARSER_ERROR;
        }

        // Mark the old rule ID for deletion
        if (old_int_rule_id >= 0) {
            old_int_rule_ids_vec.push_back((uint32_t) old_int_rule_id);
        }

        // Now it is safe to append this rule for installation
        rules_str += rule;

        glb_rule_ids_vec.push_back(rule_id);
        int_rule_ids_vec.push_back(int_rule_id);

        int_rule_ids[installed_rules_nb] = int_rule_id;

        installed_rules_nb++;
        it++;

        if (_verbose) {
            _errh->message("\n");
        }
    }

    assert(installed_rules_nb == glb_rule_ids_vec.size());
    assert(installed_rules_nb == int_rule_ids_vec.size());

    // Initialize the counters for the new internal rule IDs
    _flow_rule_cache->initialize_rule_counters(int_rule_ids, installed_rules_nb);
    // Now delete the buffer to avoid memory leaks
    free(int_rule_ids);

    uint32_t old_rules_to_delete = old_int_rule_ids_vec.size();

    // First delete existing rules (if any)
    if (flow_rules_delete(old_int_rule_ids_vec, false) != old_rules_to_delete) {
        return FLOWRULEPARSER_ERROR;
    }

    if (_debug_mode) {
        // Verify that what we deleted is not in the flow cache anynore
        assert(flow_rules_verify_absence(old_int_rule_ids_vec) == FLOWRULEPARSER_SUCCESS);
    }

    RuleTiming rits(_port_id);
    rits.start = Timestamp::now_steady();

    // Install in the NIC as a batch
    if (flow_rules_install(rules_str, installed_rules_nb) != FLOWRULEPARSER_SUCCESS) {
        return FLOWRULEPARSER_ERROR;
    }

    rits.end = Timestamp::now_steady();

    rits.update(installed_rules_nb);
    add_rule_inst_stats(rits);

    if (_debug_mode) {
        // Verify that what we inserted is in the flow cache
        assert(flow_rules_verify_presence(int_rule_ids_vec) == FLOWRULEPARSER_SUCCESS);
    }

    // Debugging stuff
    if (_debug_mode || _verbose) {
        capacity = (capacity == 0) ? (int32_t) flow_rules_count() : capacity;
        flow_rule_consistency_check(capacity);
    }

    _errh->message(
        "DPDK Flow Rule Manager (port %u): Successfully installed %" PRIu32 "/%" PRIu32 " rules in %.2f ms at the rate of %.3f rules/sec",
        _port_id, installed_rules_nb, rules_to_install, rits.latency_ms, rits.rules_per_sec
    );

    return (int32_t) installed_rules_nb;
}

/**
 * Installs a set of flow rules read from a file.
 * First the flow rules are parsed and inserted as a single string;
 * Then the flow rules are tokenized to facilitate their insertion in the flow cache.
 *
 * @args filename: the file that contains the flow rules
 * @return the number of flow rules being installed, otherwise a negative integer
 */
int32_t
FlowRuleManager::flow_rules_add_from_file(const String &filename)
{
    HashMap<uint32_t, String> rules_map;
    const String rules_str = (const String) flow_rules_from_file_to_string(filename);

    if (rules_str.empty()) {
        return (int32_t) _errh->error("DPDK Flow Rule Manager (port %u): Failed to add rules due to empty input from file", _port_id);
    }

    // Tokenize them to facilitate the insertion in the flow cache
    Vector<String> rules_vec = rules_str.trim_space().split('\n');

    for (uint32_t i = 0; i < rules_vec.size(); i++) {
        String rule = rules_vec[i] + "\n";

        // Obtain the right internal rule ID
        uint32_t next_int_rule_id = _flow_rule_cache->next_internal_rule_id();

        // Add rule to the map
        rules_map.insert((uint32_t) next_int_rule_id, rule);
    }

    return flow_rules_update(rules_map, false);
}

/**
 * Translates a set of newline-separated flow rules into flow rule objects and installs them in a NIC.
 *
 * @args rules: a string of newline-separated flow rules
 * @args rules_nb: the number of flow rules to installS
 * @args verbose: if true, prints messages (dafults to true)
 * @return installation status
 */
int
FlowRuleManager::flow_rules_install(const String &rules, const uint32_t &rules_nb, const bool verbose)
{
    // Only active instances can configure a NIC
    if (!active()) {
	//if(unlikely(verbose))
	 _errh->error("DPDK Flow Rule Manager (port %u): Inactive instance cannot install rules", _port_id);
        return FLOWRULEPARSER_ERROR;
    }

    uint32_t rules_before = flow_rules_count_explicit();

    // TODO: Fix DPDK to return proper status
    int res = flow_rule_parser_parse(_parser, (const char *) rules.c_str(), _errh);

    uint32_t rules_after = flow_rules_count_explicit();

    if (res >= 0) {
        // Workaround DPDK's deficiency to report rule installation issues
        if ((rules_before + rules_nb) != rules_after) {
	    if(unlikely(verbose))
		_errh->message("DPDK Flow Rule Manager (port %u): Flow installation failed - Has %" PRIu32 ", but expected %" PRIu32 " rules", _port_id, rules_after, rules_before + rules_nb);
            return FLOWRULEPARSER_ERROR;
        } else {
	    if(unlikely(verbose))
	        _errh->message("DPDK Flow Rule Manager (port %u): Parsed and installed a batch of %" PRIu32 " rules", _port_id, rules_nb);
	    return FLOWRULEPARSER_SUCCESS;
        }
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

    if ((rules_before + rules_nb) != rules_after) {
        _errh->error("DPDK Flow Rule Manager (port %u): Partially installed %" PRIu32 "/%" PRIu32 " rules", _port_id, (rules_after - rules_before), rules_nb);
    }
    _errh->error("DPDK Flow Rule Manager (port %u): Failed to parse rules due to %s", _port_id, error.c_str());

    return FLOWRULEPARSER_ERROR;
}

/**
 * Translates a flow rule into a flow rule object and installs it in a NIC.
 * If with_cache is false, then it returns without interacting with Flow Cache.
 *
 * @args int_rule_id: a flow rule's internal ID
 * @args rule_id: a flow rule's global ID
 * @args core_id: a CPU core ID associated with this flow rule
 * @args rule: a flow rule as a string
 * @args with_cache: if true, the flow cache is updated accordingly (defaults to true)
 * @args verbose: if true, print messages about the operations (defaults to true)
 * @return installation status
 */
int
FlowRuleManager::flow_rule_install(const uint32_t &int_rule_id, const uint32_t &rule_id, const int &core_id, const String &rule, const bool with_cache, const bool verbose)
{
    // Insert in NIC
    if (flow_rules_install(rule, 1, verbose) != FLOWRULEPARSER_SUCCESS) {
        return FLOWRULEPARSER_ERROR;
    }

    // Update flow cache, If asked to do so
    if (with_cache) {
        int32_t old_int_rule_id = _flow_rule_cache->internal_from_global_rule_id(rule_id);
        if (!_flow_rule_cache->update_rule_in_flow_cache(core_id, rule_id, int_rule_id, rule)) {
            return FLOWRULEPARSER_ERROR;
        } else {
            uint32_t int_rule_ids[1] = {(uint32_t) int_rule_id};
            _flow_rule_cache->initialize_rule_counters(int_rule_ids, 1);
        }
        if (old_int_rule_id >= 0) {
            uint32_t old_int_rule_ids[1] = {(uint32_t) old_int_rule_id};
            return (flow_rules_delete(old_int_rule_ids, 1) == 1);
        }
        return FLOWRULEPARSER_SUCCESS;
    }

    return FLOWRULEPARSER_SUCCESS;
}

/**
 * Verifies that a list of new flow rule IDs is present in the NIC, while
 * a list of old flow rule IDs is absent from the same NIC.
 *
 * @args int_rule_ids_vec: a list of new flow rules to verify their presence
 * @args old_int_rule_ids_vec: a list of old flow rules to verify their absense
 * @return verification status
 */
int
FlowRuleManager::flow_rules_verify(const Vector<uint32_t> &int_rule_ids_vec, const Vector<uint32_t> &old_int_rule_ids_vec)
{
    bool verified = flow_rules_verify_presence(int_rule_ids_vec);
    return verified & flow_rules_verify_absence(old_int_rule_ids_vec);
}

/**
 * Verifies that a list of new flow rule IDs is present in the NIC.
 *
 * @args int_rule_ids_vec: a list of new flow rules to verify their presence
 * @return presence status
 */
int
FlowRuleManager::flow_rules_verify_presence(const Vector<uint32_t> &int_rule_ids_vec)
{
    bool verified = true;

    _errh->message("====================================================================================================");

    for (auto int_id : int_rule_ids_vec) {
        if (!flow_rule_get(int_id)) {
            verified = false;
            String message = "DPDK Flow Rule Manager (port " + String(_port_id) + "): Rule " + String(int_id) + " is not in the NIC";
            if (_verbose) {
                String rule = _flow_rule_cache->get_rule_by_internal_id(int_id);
                assert(!rule.empty());
                message += " " + rule;
            }
            _errh->error("%s", message.c_str());
        }
    }

    _errh->message("Presence of internal rule IDs: %s", verified ? "Verified" : "Not verified");
    _errh->message("====================================================================================================");

    return verified ? FLOWRULEPARSER_SUCCESS : FLOWRULEPARSER_ERROR;
}

/**
 * Verifies that a list of old flow rule IDs is absent from the NIC.
 *
 * @args old_int_rule_ids_vec: a list of old flow rules to verify their absense
 * @return absence status
 */
int
FlowRuleManager::flow_rules_verify_absence(const Vector<uint32_t> &old_int_rule_ids_vec)
{
    bool verified = true;

    _errh->message("====================================================================================================");

    for (auto int_id : old_int_rule_ids_vec) {
        if (flow_rule_get(int_id)) {
            verified = false;
            String message = "DPDK Flow Rule Manager (port " + String(_port_id) + "): Rule " + String(int_id) + " is still in the NIC";
            if (_verbose) {
                String rule = _flow_rule_cache->get_rule_by_internal_id(int_id);
                assert(!rule.empty());
                message += " " + rule;
            }
            _errh->error("%s", message.c_str());
        }
    }

    _errh->message("Absence of internal rule IDs: %s", verified ? "Verified" : "Not verified");
    _errh->message("====================================================================================================");

    return verified ? FLOWRULEPARSER_SUCCESS : FLOWRULEPARSER_ERROR;
}

/**
 * Returns a flow rule object of a specific NIC with specific internal flow rule ID.
 *
 * @args int_rule_id: an internal flow rule ID
 * @return a flow rule object
 */
struct port_flow *
FlowRuleManager::flow_rule_get(const uint32_t &int_rule_id)
{
    struct rte_port *port = get_port(_port_id);
    if (!port->flow_list) {
        return NULL;
    }

    for (struct port_flow *pf = port->flow_list; pf != NULL; pf = pf->next) {
        if (pf->id == int_rule_id) {
            return pf;
        }
    }

    return NULL;
}

/**
 * Removes a vector-based batch of flow rule objects from a NIC.
 * If with_cache is true, then this batch of flow rules is also deleted from the flow cache.
 *
 * @args old_int_rule_ids_vec: a vector of internal flow rule IDs
 * @args with_cache: if true, the flow cache is updated accordingly (defaults to true)
 * @return the number of deleted flow rules upon success, otherwise a negative integer
 */
int32_t
FlowRuleManager::flow_rules_delete(const Vector<uint32_t> &old_int_rule_ids_vec, const bool with_cache)
{
    uint32_t rules_to_delete = old_int_rule_ids_vec.size();
    if (rules_to_delete == 0) {
        return rules_to_delete;
    }

    uint32_t int_rule_ids[rules_to_delete];

    uint32_t rules_nb = 0;
    auto it = old_int_rule_ids_vec.begin();
    while (it != old_int_rule_ids_vec.end()) {
        int_rule_ids[rules_nb++] = (uint32_t) *it;
        it++;
    }

    return flow_rules_delete(int_rule_ids, rules_nb, with_cache);
}

/**
 * Removes a batch of flow rule objects from a NIC.
 * If with_cache is true, then this batch of flow rules is also deleted from the flow cache.
 *
 * @args int_rule_ids: an array of internal flow rule IDs to delete
 * @args rules_nb: the number of flow rules to delete
 * @args with_cache: if true, the flow cache is updated accordingly (defaults to true)
 * @return the number of deleted flow rules upon success, otherwise a negative integer
 */
int32_t
FlowRuleManager::flow_rules_delete(uint32_t *int_rule_ids, const uint32_t &rules_nb, const bool with_cache)
{
    // Only active instances can configure a NIC
    if (!active()) {
        return _errh->error("DPDK Flow Rule Manager (port %u): Inactive instance cannot remove rules", _port_id);
    }

    // Inputs' sanity check
    if ((!int_rule_ids) || (rules_nb == 0)) {
        return _errh->error("DPDK Flow Rule Manager (port %u): No rules to remove", _port_id);
    }

    RuleTiming rdts(_port_id);
    rdts.start = Timestamp::now_steady();

    // TODO: For N rules, port_flow_destroy calls rte_flow_destroy N times.
    // TODO: If one of the rule IDs in this array is invalid, port_flow_destroy still succeeds.
    //       DPDK must act upon these issues.
    if (port_flow_destroy(_port_id, (uint32_t) rules_nb, (const uint32_t *) int_rule_ids) != FLOWRULEPARSER_SUCCESS) {
        return _errh->error(
            "DPDK Flow Rule Manager (port %u): Failed to remove a batch of %" PRIu32 " rules",
            _port_id, rules_nb
        );
    }

    rdts.end = Timestamp::now_steady();

    rdts.update((uint32_t) rules_nb);
    add_rule_del_stats(rdts);

    // Update flow cache
    if (with_cache) {
        // First calibrate the cache
        flow_rule_cache_calibrate(int_rule_ids, rules_nb);

        String rule_ids_str = _flow_rule_cache->delete_rules_by_internal_id(int_rule_ids, rules_nb);
        if (rule_ids_str.empty()) {
            return FLOWRULEPARSER_ERROR;
        }
    }

    _errh->message(
        "DPDK Flow Rule Manager (port %u): Successfully deleted %" PRIu32 " rules in %.2f ms at the rate of %.3f rules/sec",
        _port_id, rules_nb, rdts.latency_ms, rdts.rules_per_sec
    );

    return rules_nb;
}

/**
 * Restricts ingress traffic to the defined flow rules.
 * In case ingress traffic does not match any of the defined rules,
 * it will be dropped by the NIC.
 *
 * @args port_id: the ID of the NIC
 * @args set: non-zero to enter isolated mode.
 * @return 0 upon success, otherwise a negative number if isolation fails
 */
int
FlowRuleManager::flow_rules_isolate(const portid_t &port_id, const int &set)
{
#if RTE_VERSION >= RTE_VERSION_NUM(17,8,0,0)
    if (port_flow_isolate(port_id, set) != FLOWRULEPARSER_SUCCESS) {
        ErrorHandler *errh = ErrorHandler::default_handler();
        return errh->error(
            "DPDK Flow Rule Manager (port %u): Failed to restrict ingress traffic to the defined flow rules", port_id
        );
    }

    return 0;
#else
    ErrorHandler *errh = ErrorHandler::default_handler();
    return errh->error(
        "DPDK Flow Rule Manager (port %u): Flow isolation is supported since DPDK 17.08", port_id
    );
#endif
}


/**
 * Queries the statistics of a NIC flow rule.
 *
 * @args int_rule_id: a flow rule's internal ID
 * @args matched_pkts: a reference for a flow rule's number of matched packets
 * @args matched_bytes: a reference for a flow rule's number of matched bytes
 * @return flow rule statistics as a string
 */
String
FlowRuleManager::flow_rule_query(const uint32_t &int_rule_id, int64_t &matched_pkts, int64_t &matched_bytes)
{
    // Only active instances can query a NIC
    if (!active()) {
        _errh->error(
            "DPDK Flow Rule Manager (port %u): Inactive instance cannot query flow rule #%" PRIu32, _port_id, int_rule_id);
        return "";
    }

    struct rte_flow_error error;
    struct rte_port *port;
    struct port_flow *pf;
    struct rte_flow_action *action = 0;
    struct rte_flow_query_count query;

    port = get_port(_port_id);
    if (!port->flow_list || (flow_rules_count() == 0)) {
        _errh->message("DPDK Flow Rule Manager (port %u): No flow rules to query", _port_id);
        return "";
    }

    // Find the desired flow rule
    for (pf = port->flow_list; pf; pf = pf->next) {
        if (pf->id == int_rule_id) {
        #if RTE_VERSION >= RTE_VERSION_NUM(18,11,0,0)
            action = pf->rule.actions;
        #else
            action = pf->actions;
        #endif
            break;
        }
    }
    if (!pf || !action) {
        _errh->message(
            "DPDK Flow Rule Manager (port %u): No stats for invalid flow rule with ID %" PRIu32, _port_id, int_rule_id);
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
            "DPDK Flow Rule Manager (port %u): No count instruction for flow rule with ID %" PRIu32, _port_id, int_rule_id);
        return "";
    }

    // Poisoning to make sure PMDs update it in case of error
    memset(&error, 0x55, sizeof(error));
    memset(&query, 0, sizeof(query));

#if RTE_VERSION >= RTE_VERSION_NUM(18,5,0,0)
    if (rte_flow_query(_port_id, pf->flow, action, &query, &error) < 0) {
#else
    if (rte_flow_query(_port_id, pf->flow, action->type, &query, &error) < 0) {
#endif
        _errh->message(
            "DPDK Flow Rule Manager (port %u): Failed to query stats for flow rule with ID %" PRIu32, _port_id, int_rule_id);
        return "";
    }

    if (query.hits_set == 1) {
        _flow_rule_cache->set_matched_packets(int_rule_id, query.hits);
        matched_pkts = query.hits;
    }
    if (query.bytes_set == 1) {
        _flow_rule_cache->set_matched_bytes(int_rule_id, query.bytes);
        matched_bytes = query.bytes;
    }

    StringAccum stats;

    stats << "hits_set: " << query.hits_set << ", "
          << "bytes_set: " << query.bytes_set << ", "
          << "hits: " << query.hits << ", "
          << "bytes: " << query.bytes;

    return stats.take_string();
}

/**
 * Reports NIC's aggregate flow rule statistics.
 *
 * @return NIC's aggregate flow rule statistics as a string
 */
String
FlowRuleManager::flow_rule_aggregate_stats()
{
    // Only active instances might have statistics
    if (!active()) {
        return "";
    }

    struct rte_port *port = get_port(_port_id);
    if (!port->flow_list || (flow_rules_count() == 0)) {
        _errh->warning("DPDK Flow Rule Manager (port %u): No aggregate statistics due to no traffic", _port_id);
        return "";
    }

    int64_t tot_pkts = 0;
    int64_t tot_bytes = 0;
    HashTable<uint16_t, int64_t> pkts_in_queue;
    HashTable<uint16_t, int64_t> bytes_in_queue;

    // Traverse the list of installed flow rules
    for (struct port_flow *pf = port->flow_list; pf != NULL; pf = pf->next) {
        uint32_t id = pf->id;
    #if RTE_VERSION >= RTE_VERSION_NUM(18,11,0,0)
        const struct rte_flow_action *action = pf->rule.actions;
    #else
        const struct rte_flow_action *action = pf->actions;
    #endif

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
        if (pkts_in_queue.find((uint16_t) queue) == pkts_in_queue.end()) {
            pkts_in_queue[(uint16_t) queue] = 0;
            bytes_in_queue[(uint16_t) queue] = 0;
        }

        // Get currest state of the packet and byte counters from the device
        int64_t matched_pkts = 0;
        int64_t matched_bytes = 0;
        flow_rule_query(id, matched_pkts, matched_bytes);

        // Count packets and bytes per queue as well as across queues
        pkts_in_queue[(uint16_t) queue] += matched_pkts;
        bytes_in_queue[(uint16_t) queue] += matched_bytes;
        tot_pkts += matched_pkts;
        tot_bytes += matched_bytes;
    }

    uint16_t queues_nb = pkts_in_queue.size();
    if (queues_nb == 0) {
        _errh->warning("DPDK Flow Rule Manager (port %u): No queues to produce aggregate statistics", _port_id);
        return "";
    }

    float perfect_pkts_in_queue = (float) tot_pkts / (float) queues_nb;
    float perfect_bytes_in_queue = (float) tot_bytes / (float) queues_nb;

    short tab_size = 26;
    StringAccum aggr_stats;
    aggr_stats.snprintf(tab_size, "%26s", "QUEUE");
    aggr_stats.snprintf(tab_size, "%26s", "RECEIVED PACKETS").snprintf(tab_size, "%26s", "PERF. BALANCED PACKETS");
    aggr_stats.snprintf(tab_size, "%26s", "RECEIVED BYTES").snprintf(tab_size, "%26s", "PERF. BALANCED BYTES");
    aggr_stats.snprintf(tab_size, "%26s", "PACKET IMBALANCE(%)").snprintf(tab_size, "%26s", "BYTE IMBALANCE(%)");
    aggr_stats << "\n";
    float imbalanced_pkts = 0.0;
    float imbalanced_bytes = 0.0;
    for (uint16_t i = 0; i < queues_nb; i++) {
        imbalanced_pkts += abs((float)pkts_in_queue[i] - perfect_pkts_in_queue);
        imbalanced_bytes += abs((float)bytes_in_queue[i] - perfect_bytes_in_queue);
        float q_pkt_imb_ratio  = (abs((float)  pkts_in_queue[i] -  perfect_pkts_in_queue) /  perfect_pkts_in_queue)*100;
        float q_byte_imb_ratio = (abs((float) bytes_in_queue[i] - perfect_bytes_in_queue) / perfect_bytes_in_queue)*100;

        aggr_stats.snprintf(tab_size, "%26d", i);
        aggr_stats.snprintf(tab_size, "%26" PRId64,  pkts_in_queue[i]).snprintf(tab_size, "%26.2f",  perfect_pkts_in_queue);
        aggr_stats.snprintf(tab_size, "%26" PRId64, bytes_in_queue[i]).snprintf(tab_size, "%26.2f", perfect_bytes_in_queue);
        aggr_stats.snprintf(tab_size, "%26.2f", q_pkt_imb_ratio).snprintf(tab_size, "%26.2f", q_pkt_imb_ratio);
        aggr_stats << "\n";
    }

    float pkt_imbalance_ratio  =  (perfect_pkts_in_queue == 0) ? 0.0 : (imbalanced_pkts / perfect_pkts_in_queue)*100;
    float byte_imbalance_ratio = (perfect_bytes_in_queue == 0) ? 0.0 : (imbalanced_bytes/perfect_bytes_in_queue)*100;

    aggr_stats << "\n";
    aggr_stats.snprintf(tab_size, "%26s", "TOTAL PACKET IMBALANCE(%)").snprintf(tab_size, "%26s", "TOTAL BYTE IMBALANCE(%)");
    aggr_stats << "\n";
    aggr_stats.snprintf(tab_size, "%26.2f", pkt_imbalance_ratio).snprintf(tab_size, "%26.2f", byte_imbalance_ratio);
    aggr_stats << "\n";

    return aggr_stats.take_string();
}

/**
 * Return the explicit flow rule counter for a particular NIC.
 *
 * @return the number of flow rules being installed
 */
uint32_t
FlowRuleManager::flow_rules_with_hits_count()
{
    // Only active instances might have statistics
    if (!active()) {
        return 0;
    }

    struct rte_port *port = get_port(_port_id);
    if (!port->flow_list || (flow_rules_count() == 0)) {
        _errh->warning("DPDK Flow Rule Manager (port %u): No counter for flow rules with hits due to no traffic", _port_id);
        return 0;
    }

    uint32_t rules_with_hits = 0;

    // Traverse the list of installed flow rules
    for (struct port_flow *pf = port->flow_list; pf != NULL; pf = pf->next) {
        uint32_t id = pf->id;

        // Get currest state of the packet and byte counters from the device
        int64_t matched_pkts = 0;
        int64_t matched_bytes = 0;
        flow_rule_query(id, matched_pkts, matched_bytes);

        // This rule has hits
        if (matched_pkts > 0) {
            rules_with_hits++;
        }
    }

    return rules_with_hits;
}

/**
 * Return the flow rule counter for a particular NIC
 * by looking at the flow rule cache.
 *
 * @return the number of flow rules being installed
 */
uint32_t
FlowRuleManager::flow_rules_count()
{
    return _flow_rule_cache->get_rule_counter();
}

/**
 * Return the flow rule counter for a particular NIC
 * by traversing the list of flow rules.
 *
 * @return the number of flow rules being installed
 */
uint32_t
FlowRuleManager::flow_rules_count_explicit()
{
    // Only active instances might have some rules
    if (!active()) {
        return 0;
    }

    uint32_t rules_nb = 0;

    struct rte_port *port = get_port(_port_id);
    if (!port->flow_list) {
        if (verbose()) {
            _errh->message("DPDK Flow Rule Manager (port %u): No flow rules", _port_id);
        }
        return rules_nb;
    }

    // Traverse the list of installed flow rules
    for (struct port_flow *pf = port->flow_list; pf != NULL; pf = pf->next) {
        rules_nb++;
    }

    return rules_nb;
}

/**
 * Compares NIC and cache rule counts and asserts inconsistency.
 */
void
FlowRuleManager::nic_and_cache_counts_agree()
{
    uint32_t nic_rules   = flow_rules_count_explicit();
    uint32_t cache_rules = flow_rules_count();

    if (_verbose) {
        _errh->message("NIC with %" PRIu32 " rules and Flow Cache with %" PRIu32 " rules", nic_rules, cache_rules);
    }
    assert(nic_rules == cache_rules);
}

/**
 * Lists all of a NIC's flow rules.
 *
 * @args only_matching_rules: If true, only rules that matched some traffic will be returned
 *                            Defaults to false, which means all rules are returned.
 * @return a string of NIC flow rules (each in a different line)
 */
String
FlowRuleManager::flow_rules_list(const bool only_matching_rules)
{
    if (!active()) {
        return "DPDK Flow Rule Manager is inactive";
    }

    struct rte_port *port = get_port(_port_id);
    if (!port->flow_list || (flow_rules_count() == 0)) {
        _errh->error("DPDK Flow Rule Manager (port %u): No flow rules to list", _port_id);
        return "No flow rules";
    }

    // Sort flows by group, priority, and ID
    struct port_flow *sorted_rules = NULL;
    flow_rules_sort(port, &sorted_rules);

    StringAccum rules_list;

    // Traverse and print the sorted list of installed flow rules
    for (struct port_flow *pf = sorted_rules; pf != NULL; pf = pf->tmp) {
        uint32_t id = pf->id;
    #if RTE_VERSION >= RTE_VERSION_NUM(18,11,0,0)
        const struct rte_flow_item *item = pf->rule.pattern;
        const struct rte_flow_action *action = pf->rule.actions;
        const struct rte_flow_attr *attr = pf->rule.attr;
    #else
        const struct rte_flow_item *item = pf->pattern;
        const struct rte_flow_action *action = pf->actions;
        const struct rte_flow_attr *attr = (const rte_flow_attr *) &pf->attr;
    #endif

        // Get currest state of the packet and byte counters from the device
        int64_t matched_pkts = 0;
        int64_t matched_bytes = 0;
        flow_rule_query(id, matched_pkts, matched_bytes);

        // Skip rule without match, if requested by the user
        if (only_matching_rules && ((matched_pkts == 0) || (matched_bytes == 0))) {
            continue;
        }

        rules_list << "Flow rule #" << id << ": [";
        rules_list << "Group: " << attr->group << ", Prio: " << attr->priority << ", ";
        rules_list << "Scope: " << (attr->ingress == 1 ? "ingress" : "-");
        rules_list << "/" << (attr->egress == 1 ? "egress" : "-");

    #if RTE_VERSION >= RTE_VERSION_NUM(18,5,0,0)
        rules_list << "/" << (attr->transfer == 1 ? "transfer" : "-");
    #endif
        rules_list << ", ";
        rules_list << "Matches:";

        while (item->type != RTE_FLOW_ITEM_TYPE_END) {
            if (item->type != RTE_FLOW_ITEM_TYPE_VOID) {
                rules_list << " ";
                rules_list << flow_item[item->type];
            }
            ++item;
        }

        rules_list << " => ";
        rules_list << "Actions:";

        while (action->type != RTE_FLOW_ACTION_TYPE_END) {
            if (action->type != RTE_FLOW_ACTION_TYPE_VOID) {
                rules_list << " ";
                rules_list << flow_action[action->type];
            }

            ++action;
        }

        // There is a valid index for flow rule counters
        if (_flow_rule_cache->internal_rule_id_exists(id)) {
            rules_list << ", ";
            rules_list << "Stats: ";
            rules_list << "Matched packets: " << matched_pkts << ", ";
            rules_list << "Matched bytes: " << matched_bytes;
        }

        rules_list << "]\n";
    }

    if (rules_list.empty()) {
        rules_list << "No flow rules";
        if (only_matching_rules) {
            rules_list << " with packet hits";
        }
    }

    return rules_list.take_string();
}

/**
 * Lists all of a NIC's internal flow rule IDs.
 *
 * @args from_nic: boolean flag to indicate the source (i.e., true for NIC or false for cache)
 * @return a string of space-separated internal flow rule IDs, otherwise a relevant message
 */
String
FlowRuleManager::flow_rule_ids_internal(const bool from_nic)
{
    if (!active()) {
        return "DPDK Flow Rule Manager is inactive";
    }

    if (from_nic) {
        return flow_rule_ids_internal_nic();
    }
    return flow_rule_ids_internal_cache();
}

/**
 * Lists all of a NIC's internal flow rule IDs.
 *
 * @return a string of space-separated internal flow rule IDs, otherwise a relevant message
 */
String
FlowRuleManager::flow_rule_ids_internal_nic()
{
    struct rte_port *port = get_port(_port_id);
    if (!port->flow_list || (flow_rules_count() == 0)) {
        _errh->error("DPDK Flow Rule Manager (port %u): No flow rule IDs to list", _port_id);
        return "";
    }

    struct port_flow *list = NULL;

    // Sort flows by group, priority, and ID
    struct port_flow *sorted_rules = NULL;
    flow_rules_sort(port, &sorted_rules);

    String rule_ids_str = "";

    // Traverse the sorted list of installed flow rules and keep their IDs
    for (struct port_flow *pf = sorted_rules; pf != NULL; pf = pf->tmp) {
        assert(flow_rule_get(pf->id));
        rule_ids_str += String(pf->id) + " ";
    }

    if (rule_ids_str.empty()) {
        return "";
    }

    return rule_ids_str.trim_space();
}

/**
 * Lists all of a NIC's internal flow rule IDs from the cache.
 *
 * @return a string of space-separated internal flow rule IDs, otherwise a relevant message
 */
String
FlowRuleManager::flow_rule_ids_internal_cache()
{
    if (!active()) {
        return "DPDK Flow Rule Manager is inactive";
    }

    Vector<uint32_t> rule_ids = _flow_rule_cache->internal_rule_ids();
    String rule_ids_str = "";

    for (uint32_t i = 0; i < rule_ids.size(); ++i) {
        rule_ids_str += String(rule_ids[i]) + " ";
    }

    if (rule_ids_str.empty()) {
        return "";
    }

    return rule_ids_str.trim_space();
}

/**
 * Lists all of a NIC's internal flow rule IDs from the cache.
 *
 * @return a string of space-separated internal flow rule IDs, otherwise a relevant message
 */
String
FlowRuleManager::flow_rule_ids_internal_counters()
{
    if (!active()) {
        return "DPDK Flow Rule Manager is inactive";
    }

    Vector<uint32_t> rule_ids = _flow_rule_cache->internal_rule_ids_counters();
    String rule_ids_str = "";

    for (uint32_t i = 0; i < rule_ids.size(); ++i) {
        rule_ids_str += String(rule_ids[i]) + " ";
    }

    return rule_ids_str.trim_space();
}

/**
 * Lists all of a NIC's global flow rule IDs.
 *
 * @return a string of space-separated global flow rule IDs, otherwise a relevant message
 */
String
FlowRuleManager::flow_rule_ids_global()
{
    if (!active()) {
        return "DPDK Flow Rule Manager is inactive";
    }

    Vector<uint32_t> rule_ids = _flow_rule_cache->global_rule_ids();
    String rule_ids_str = "";

    for (uint32_t i = 0; i < rule_ids.size(); ++i) {
        rule_ids_str += String(rule_ids[i]) + " ";
    }

    return rule_ids_str.trim_space();
}

/**
 * Sorts a list of flow rules by group, priority, and ID.
 *
 * @args port: flow rule list to sort
 * @return a sorted flow rule list
 */
void
FlowRuleManager::flow_rules_sort(struct rte_port *port, struct port_flow **sorted_rules)
{
    if (!port || !port->flow_list) {
        _errh->error("DPDK Flow Rule Manager (port %u): Cannot sort empty flow rules' list", _port_id);
        return;
    }

    for (struct port_flow *pf = port->flow_list; pf != NULL; pf = pf->next) {
        struct port_flow **tmp;
    #if RTE_VERSION >= RTE_VERSION_NUM(18,11,0,0)
        const struct rte_flow_attr *curr = pf->rule.attr;
    #else
        const struct rte_flow_attr *curr = (const rte_flow_attr *) &pf->attr;
    #endif

        for (tmp = sorted_rules; *tmp; tmp = &(*tmp)->tmp) {
        #if RTE_VERSION >= RTE_VERSION_NUM(18,11,0,0)
            const struct rte_flow_attr *comp = (*tmp)->rule.attr;
        #else
            const struct rte_flow_attr *comp = (const rte_flow_attr *) &((*tmp)->attr);
        #endif

            // Sort flows by group, priority, and ID
            if (curr->group > comp->group ||
               ((curr->group == comp->group) && (curr->priority > comp->priority)) ||
               ((curr->group == comp->group) && (curr->priority == comp->priority) && (pf->id > (*tmp)->id)))
                    continue;
            break;
        }
        pf->tmp = *tmp;
        *tmp = pf;
    }
}

/**
 * Flushes all of the flow rules from a NIC associated with this Flow Rule Manager instance.
 *
 * @return the number of flow rules being flushed
 */
uint32_t
FlowRuleManager::flow_rules_flush()
{
    // Only active instances can configure a NIC
    if (!active()) {
        _errh->message("DPDK Flow Rule Manager (port %u): Nothing to flush", _port_id);
        return 0;
    }

    RuleTiming rdts(_port_id);
    rdts.start = Timestamp::now_steady();

    uint32_t rules_before_flush = flow_rules_count_explicit();
    if (rules_before_flush == 0) {
        return 0;
    }

    // Successful flush means zero rules left
    if (port_flow_flush(_port_id) != FLOWRULEPARSER_SUCCESS) {
        uint32_t rules_after_flush = flow_rules_count_explicit();
        _errh->warning("DPDK Flow Rule Manager (port %u): Flushed only %" PRIu32 " rules", _port_id, (rules_before_flush - rules_after_flush));
        return (rules_before_flush - rules_after_flush);
    }

    rdts.end = Timestamp::now_steady();

    rdts.update(rules_before_flush);
    add_rule_del_stats(rdts);

    // Successful flush, now flush also the cache
    _flow_rule_cache->flush_rules_from_cache();

    if (_verbose) {
        _errh->message(
            "DPDK Flow Rule Manager (port %u): Successfully flushed %" PRIu32 " rules in %.0f ms at the rate of %.3f rules/sec",
            _port_id, rules_before_flush, rdts.latency_ms, rdts.rules_per_sec
        );
    }

    return rules_before_flush;
}

/**
 * Filters unwanted components from a flow rule and returns an updated flow rule by reference.
 *
 * @args rule: a flow rule to filter
 * @return boolean status
 */
bool
FlowRuleManager::flow_rule_filter(String &rule)
{
    const char *prefix = "flow create";
    size_t prefix_len = strlen(prefix);
    size_t rule_len = rule.length();

    // Fishy
    if (rule_len <= prefix_len) {
        return false;
    }

    // Rule starts with prefix
    if (strncmp(prefix, rule.c_str(), prefix_len) == 0) {
        // Skip the prefix
        rule = rule.substring(prefix_len + 1);

        // Remove a potential port ID and spaces before the actual rule
        while (true) {
            if (!isdigit(rule[0]) && (rule[0] != ' ')) {
                break;
            }
            rule = rule.substring(1);
        }

        return (rule.length() > 0) ? true : false;
    }

    return true;
}

/**
 * Returns a flow rule token after an input keyword.
 *
 * @args rule: a flow rule to parse
 * @args keyword: a keyword to match before the token
 * @return rule token after keyword upon success, otherwise empty string
 */
String
FlowRuleManager::fetch_token_after_keyword(char *rule, const String &keyword)
{
    char *p = strstr(rule, keyword.c_str());
    if(!p) {
        click_chatter("Keyword '%s' is not in '%s'", keyword.c_str(), rule);
        return "";
    }

    p += (keyword.length() + 1);

    if (!p) {
        click_chatter("No token follows keyword '%s' in '%s'", keyword.c_str(), rule);
        return "";
    }

    // Return first token after the keyword
    return String(p).trim_space().trim_space_left().split(' ')[0];
}

/**
 * Computes the minimum, average, and maximum flow rule installation/deletion rate (rules/sec) or latency (ms)
 * across the entire set of such operations. The last argument denotes whether to compute latency or rate.
 *
 * @args min: variable to store the minimum installation/deletion rate
 * @args mean: variable to store the average installation/deletion rate
 * @args max: variable to store the maximum installation/deletion rate
 * @args install: if true, rule installation values are returned, otherwise deletion values are returned (default is true)
 * @args latency: if true, latency values are returned, otherwise rate values are returned (default is true)
 */
void
FlowRuleManager::min_avg_max(float &min, float &mean, float &max, const bool install, const bool latency)
{
    const Vector<RuleTiming> *rule_stats_vec = 0;
    if (install) {
        rule_stats_vec = &_rule_inst_stats_map[_port_id];
    } else {
        rule_stats_vec = &_rule_del_stats_map[_port_id];
    }

    if (!rule_stats_vec) {
        _errh->warning("DPDK Flow Rule Manager (port %u): No rule statistics available", _port_id);
        return;
    }

    float sum = 0.0;
    uint32_t len = rule_stats_vec->size();
    auto it = rule_stats_vec->begin();
    while (it != rule_stats_vec->end()) {
        float value = 0.0;
        if (latency) {
            value = it->latency_ms;
        } else {
            value = it->rules_per_sec;
        }

        if (value < min) {
            min = value;
        }
        if (value > max) {
            max = value;
        }
        sum += value;

        it++;
    }

    // Set minimum properly if not updated above
    if (min == std::numeric_limits<float>::max()) {
        min = 0;
    }

    if (len == 0) {
        mean = 0;
    } else {
        mean = sum / static_cast<float>(len);
    }
}

/**
 * Performs a run-time consistency check with respect to the desired occupancy of the NIC.
 *
 * @args target_number_of_rules: desired NIC occupancy
 */
void
FlowRuleManager::flow_rule_consistency_check(const int32_t &target_number_of_rules)
{
    if (target_number_of_rules < 0) {
        _errh->error(
            "DPDK Flow Rule Manager (port %u): Cannot verify consistency with a negative number of target rules",
            get_port_id(), target_number_of_rules
        );
        return;
    }

    // First check the flow rule cache
    _flow_rule_cache->cache_consistency_check(target_number_of_rules);

    // Then the NIC with respect to the cache
    nic_consistency_check(target_number_of_rules);
}

/**
 * Performs a run-time consistency check with respect to the desired occupancy of the NIC.
 *
 * @args target_number_of_rules: desired NIC occupancy
 */
void
FlowRuleManager::nic_consistency_check(const int32_t &target_number_of_rules)
{
    String int_cache_str = flow_rule_ids_internal(false);
    String int_str = flow_rule_ids_internal(true);
    String int_counters_str = flow_rule_ids_internal_counters();
    String glb_str = flow_rule_ids_global();

    Vector<String> int_cache_vec = int_cache_str.split(' ');
    Vector<String> int_vec = int_str.split(' ');
    Vector<String> int_counters_vec = int_counters_str.split(' ');
    Vector<String> glb_vec = glb_str.split(' ');

    _errh->message("[CACHE] %" PRIu32 " Internal rule IDs: %s", int_cache_vec.size(), int_cache_str.c_str());
    _errh->message("  [NIC] %" PRIu32 " Internal rule IDs: %s", int_vec.size(), int_str.c_str());
    _errh->message("[COUNT] %" PRIu32 " Internal rule IDs: %s", int_counters_vec.size(), int_counters_str.c_str());
    _errh->message("[CACHE] %" PRIu32 "   Global rule IDs: %s", glb_vec.size(), glb_str.c_str());

    // Now the NIC
    uint32_t nic_rules = flow_rules_count_explicit();
    _errh->message("%" PRIu32 " [NIC] Rules", flow_rules_count_explicit());
    if (nic_rules != target_number_of_rules) {
        _errh->error(
            "Flow Cache (port %u): Number of rules in the NIC %" PRIu32 " does not agree with target rules %" PRId32,
            get_port_id(), nic_rules, target_number_of_rules
        );
    }

    // Perform the assertions at the end, to have full verbosity for debugging
    assert(int_cache_str == int_str);
    assert(int_counters_str == int_str);
    assert(int_vec.size() == target_number_of_rules);
    assert(nic_rules == target_number_of_rules);
    nic_and_cache_counts_agree();
}

#endif /* RTE_VERSION >= RTE_VERSION_NUM(20,2,0,0) */

CLICK_ENDDECLS
