// -*- c-basic-offset: 4; related-file-name: "flowdirector.hh" -*-
/*
 * flowdirector.cc -- library for integrating DPDK's Flow Director in Click
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

#include <click/config.h>
#include <click/straccum.hh>
#include <click/flowdirector.hh>

CLICK_DECLS

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)

#include <rte_flow.h>

/**
 * Flow Cache implementation.
 */
// Error handling
int FlowCache::ERROR = -1;
int FlowCache::SUCCESS = 0;

/**
 * Return the NIC port ID associated with this flow cache.
 *
 * @return port ID of the NIC associated with this cache
 */
portid_t
FlowCache::get_port_id()
{
    return _port_id;
}

/**
 * Return the NIC device's adress suitable to use as a FromDPDKDevice reference.
 *
 * @return port address of the NIC associated with this cache
 */
String
FlowCache::get_device_address()
{
    // TODO: Returning the PCI address would be better
    // return String(_fd->get_device()->get_device_id());
    return String(_port_id);
}

/**
 * Checks whether this flow cache contains any rules or not.
 *
 * @return true if at least one rule is in the cache, otherwise false
 */
bool
FlowCache::has_rules()
{
    return !_rules.empty();
}

/**
 * Checks whether a given global rule ID is present or not.
 *
 * @args rule_id: a global rule ID to find
 * @return true if rule_id exists, otherwise false
 */
bool
FlowCache::global_rule_id_exists(const long &rule_id)
{
    return (internal_from_global_rule_id(rule_id) >= 0) ? true : false;
}

/**
 * Checks whether an internal rule ID is present or not.
 *
 * @args int_rule_id: an internal rule ID to find
 * @return true if int_rule_id exists, otherwise false
 */
bool
FlowCache::internal_rule_id_exists(const uint32_t &int_rule_id)
{
    return (global_from_internal_rule_id(int_rule_id) >= 0) ? true : false;
}

/**
 * Traverses the map of global rule IDs to internal NIC IDs,
 * and checks whether an internal rule ID is present or not.
 *
 * @args int_rule_id: an internal rule ID to find
 * @return the global rule ID mapped to the input internal rule ID upon success,
 *         otherwise a negative integer
 */
long
FlowCache::global_from_internal_rule_id(const uint32_t &int_rule_id)
{
    if (int_rule_id < 0) {
        return (long) _errh->error("Flow Cache (port %u): Unable to verify mapping due to invalid internal NIC rule ID %" PRIu32, get_port_id(), int_rule_id);
    }

    auto it = _internal_rule_map.begin();
    while (it != _internal_rule_map.end()) {
        long r_id = it.key();
        uint32_t int_r_id = it.value();

        if (int_r_id == int_rule_id) {
            if (_verbose) {
                _errh->message("Flow Cache (port %u): Internal rule ID %" PRIu32 " is mapped to global rule ID %ld", get_port_id(), int_rule_id, r_id);
            }
            return r_id;
        }

        it++;
    }

    if (_verbose) {
        _errh->message("Flow Cache (port %u): Internal rule ID %" PRIu32 " does not exist in the flow cache", get_port_id(), int_rule_id);
    }

    return (long) ERROR;
}

/**
 * Queries the map of global rule IDs to internal NIC IDs,
 * and checks whether a global rule ID is present or not.
 *
 * @args rule_id: a global rule ID to find
 * @return the internal rule ID mapped to the input global rule ID upon success,
 *         otherwise a negative integer
 */
int32_t
FlowCache::internal_from_global_rule_id(const long &rule_id)
{
    if (rule_id < 0) {
        return (int32_t) _errh->error("Flow Cache (port %u): Unable to verify mapping due to invalid global NIC rule ID %ld", get_port_id(), rule_id);
    }

    const uint32_t *found = _internal_rule_map.findp(rule_id);
    if (!found) {
        if (_verbose) {
            _errh->message("Flow Cache (port %u): Global rule ID %ld does not exist in the flow cache", get_port_id(), rule_id);
        }
        return (int32_t) ERROR;
    }

    if (_verbose) {
        _errh->message("Flow Cache (port %u): Global rule ID %ld is mapped to internal rule ID %" PRIu32, get_port_id(), rule_id, *found);
    }

    return (int32_t) *found;
}

/**
 * Returns a string with all space-separated global rules IDs in this flow cache.
 *
 * @return string with all space-separated global rules IDs, otherwise a relevant message
 */
String
FlowCache::global_rule_ids()
{
    String rule_ids = "";

    auto ext_it = _rules.begin();
    while (ext_it != _rules.end()) {
        int core_id = ext_it.key();
        HashMap<long, String> *rules_map = ext_it.value();

        // No rules associated with this CPU core
        if (!rules_map || rules_map->empty()) {
            ext_it++;
            continue;
        }

        auto it = rules_map->begin();
        while (it != rules_map->end()) {
            long rule_id = it.key();
            rule_ids += String(rule_id) + " ";

            it++;
        }

        ext_it++;
    }

    if (rule_ids.empty()) {
        return "No flow rules";
    }

    return rule_ids.trim_space();
}

/**
 * Returns a map of rule IDs to rules.
 *
 * @args core_id: a CPU core ID
 * @return a map of rule IDs to rules associated with this core
 */
HashMap<long, String> *
FlowCache::rules_map_by_core_id(const int &core_id)
{
    if (core_id < 0) {
        _errh->error("Flow Cache (port %u): Unable to find rule map due to invalid CPU core ID %d", get_port_id(), core_id);
        return NULL;
    }

    return _rules.find(core_id);
}

/**
 * Returns a list of rules associated with a CPU core.
 *
 * @args core_id: a CPU core ID
 * @return a list of rules associated with this core
 */
Vector<String>
FlowCache::rules_list_by_core_id(const int &core_id)
{
    Vector<String> rules_vec;

    if (core_id < 0) {
        _errh->error("Flow Cache (port %u): Unable to find rules due to invalid CPU core ID %d", get_port_id(), core_id);
        return rules_vec;
    }

    HashMap<long, String> *rules_map = rules_map_by_core_id(core_id);
    if (!rules_map) {
        _errh->error("Flow Cache (port %u): No rules associated with CPU core ID %d", get_port_id(), core_id);
        return rules_vec;
    }

    auto it = rules_map->begin();
    while (it != rules_map->end()) {
        String rule = it.value();

        if (!rule.empty()) {
            rules_vec.push_back(rule);
        }

        it++;
    }

    return rules_vec;
}

/**
 * Returns the number of CPU cores that have at least one rule each.
 *
 * @return a list CPU cores that have associated rules
 */
Vector<int>
FlowCache::cores_with_rules()
{
    Vector<int> cores_with_rules;

    for (int i = 0; i < click_max_cpu_ids(); i++) {
        Vector<String> core_rules = rules_list_by_core_id(i);

        if (core_rules.empty()) {
            continue;
        }

        cores_with_rules[i] = i;
    }

    return cores_with_rules;
}

/**
 * Keeps a mapping between global and internal NIC rule IDs.
 *
 * @args rule_id: a rule ID
 * @args int_rule_id: an internal rule ID mapped to rule_id
 * @return boolean status
 */
bool
FlowCache::store_rule_id_mapping(const long &rule_id, const uint32_t &int_rule_id)
{
    if (rule_id < 0) {
        _errh->error("Flow Cache (port %u): Unable to store mapping due to invalid rule ID %ld", get_port_id(), rule_id);
        return false;
    }

    /**
     * Verify the uniqueness of this internal ID.
     * TODO: The complexity of this method is O(#OfRules)!
     * Consider removing this operation, thus assuming that the
     * controller knows what it is doing.
     */
    if (internal_rule_id_exists(int_rule_id)) {
        return false;
    }

    if (!_internal_rule_map.insert(rule_id, int_rule_id)) {
        _errh->error("Flow Cache (port %u): Failed to inserted rule mapping %ld <--> %" PRIu32, get_port_id(), rule_id, int_rule_id);
        return false;
    }

    if (_verbose) {
        _errh->message("Flow Cache (port %u): Successfully inserted rule mapping %ld <--> %" PRIu32, get_port_id(), rule_id, int_rule_id);
    }

    return true;
}

/**
 * Deletes a mapping between a global and an internal NIC rule ID.
 *
 * @args rule_id: a rule ID for which an internal ID mapping is requested
 * @return boolean status
 */
bool
FlowCache::delete_rule_id_mapping(const long &rule_id)
{
    if (rule_id < 0) {
        _errh->error("Flow Cache (port %u): Unable to delete mapping for invalid rule ID %ld", get_port_id(), rule_id);
        return false;
    }

    if (_internal_rule_map.remove(rule_id)) {
        if (_verbose) {
            _errh->message("Flow Cache (port %u): Successfully deleted mapping for rule ID %ld", get_port_id(), rule_id);
        }

        return true;
    }

    return false;
}

/**
 * Returns the next rule ID to use for insertion.
 *
 * @return next available rule ID
 */
uint32_t
FlowCache::next_internal_rule_id()
{
    return _next_rule_id;
}

/**
 * Adds a new rule to this flow cache.
 *
 * @args core_id: a CPU core ID associated with the rule
 * @args rule_id: a rule ID associated with the rule
 * @args int_rule_id: an internal rule ID associated with the rule
 * @args rule: the actual rule
 * @return true upon success, otherwise false
 */
int
FlowCache::insert_rule_in_flow_cache(const int &core_id, const long &rule_id, const uint32_t &int_rule_id, const String rule)
{
    if (core_id < 0) {
        return _errh->error("Flow Cache (port %u): Unable to add rule due to invalid CPU core ID %d", get_port_id(), core_id);
    }

    if (rule_id < 0) {
        return _errh->error("Flow Cache (port %u): Unable to add rule due to invalid rule ID %ld", get_port_id(), rule_id);
    }

    if (int_rule_id < 0) {
        return _errh->error("Flow Cache (port %u): Unable to add rule due to invalid internal rule ID %" PRIu32, get_port_id(), int_rule_id);
    }

    if (rule.empty()) {
        return _errh->error("Flow Cache (port %u): Unable to add rule due to empty input", get_port_id());
    }

    HashMap<long, String> *rules_map = rules_map_by_core_id(core_id);
    if (!rules_map) {
        _rules.insert(core_id, new HashMap<long, String>());
        rules_map = rules_map_by_core_id(core_id);
        assert(rules_map);
    }

    if (!rules_map->insert(rule_id, rule)) {
        return _errh->error("Flow Cache (port %u): Unable to add rule due to cache failure", get_port_id());
    }

    if (!store_rule_id_mapping(rule_id, int_rule_id)) {
        return _errh->error("Flow Cache (port %u): Unable to add rule mapping due to cache failure", get_port_id());
    }

    if (_verbose) {
        _errh->message(
            "Flow Cache (port %u): Rule %ld added and mapped with internal rule ID %" PRIu32 " and queue %d",
            get_port_id(), rule_id, int_rule_id, core_id
        );
    }

    return SUCCESS;
}

/**
 * Updates a rule using a two-phase commit.
 * First checks if rule exists and if so deletes it.
 * Then, inserts the new rule.
 *
 * @args core_id: a CPU core ID associated with the rule
 * @args rule_id: the rule ID of the rule to be updated
 * @args rule: the actual rule to be updated
 * @return true upon success, otherwise false
 */
bool
FlowCache::update_rule_in_flow_cache(const int &core_id, const long &rule_id, const uint32_t &int_rule_id, String rule)
{
    // First try to delete this rule, if it exists
    if (delete_rule_by_global_id(rule_id) < 0) {
        // Rule did not exist before, so initialize it now
        uint32_t int_rule_ids[1] = {int_rule_id};
        initialize_rule_counters(int_rule_ids, (const uint32_t) 1);
    }

    // Now, store this rule in this CPU core's flow cache
    return (insert_rule_in_flow_cache(core_id, rule_id, int_rule_id, rule) == SUCCESS);
}

/**
 * Deletes a rule from this flow cache using its global ID as an index.
 *
 * @args rule_id: the rule ID of the rule to be deleted
 * @return the internal rule ID being deleted upon success, otherwise a negative integer
 */
int32_t
FlowCache::delete_rule_by_global_id(const long &rule_id)
{
    if (rule_id < 0) {
        return _errh->error("Flow Cache (port %u): Unable to delete rule due to invalid global rule ID %ld", get_port_id(), rule_id);
    }

    auto it = _rules.begin();
    while (it != _rules.end()) {
        int core_id = it.key();
        HashMap<long, String> *rules_map = it.value();

        // No rules associated with this CPU core
        if (!rules_map || rules_map->empty()) {
            it++;
            continue;
        }

        // Remove rule from the cache
        if (rules_map->remove(rule_id)) {
            // Fetch the mapping of the global rule ID with the internal rule ID
            int32_t internal_rule_id = internal_from_global_rule_id(rule_id);
            if (internal_rule_id < 0) {
                return _errh->error("Flow Cache (port %u): Unable to delete rule %ld due to no internal mapping", get_port_id(), rule_id);
            }

            // Now delete this mapping
            if (!delete_rule_id_mapping(rule_id)) {
                return ERROR;
            }

            if (_verbose) {
                click_chatter("Flow Cache (port %u): Rule with ID %ld deleted from queue %d", get_port_id(), rule_id, core_id);
            }

            // Update counters
            uint32_t internal_rule_ids[1] = {(uint32_t) internal_rule_id};
            delete_rule_counters(internal_rule_ids, 1);

            return internal_rule_id;
        }

        it++;
    }

    if (_verbose) {
        click_chatter("Flow Cache (port %u): Unable to delete rule %ld due to cache miss", get_port_id(), rule_id);
    }

    return ERROR;
}

/**
 * Deletes a list of rules from this flow cache.
 *
 * @args rule_ids: an array of rule IDs to be deleted
 * @args rules_nb: the number of rule IDs to be deleted
 * @return a space-separated list of deleted internal rule IDs upon success, otherwise an empty string
 */
String
FlowCache::delete_rules_by_internal_id(const uint32_t *rule_ids, const uint32_t &rules_nb)
{
    String rule_ids_str = "";
    for (uint32_t i = 0; i < rules_nb; i++) {
        rule_ids_str += String(rule_ids[i]) + " ";
    }

    rule_ids_str = rule_ids_str.trim_space();

    return delete_rules_by_internal_id(rule_ids_str.split(' '));
}

/**
 * Deletes a list of rules from this flow cache.
 *
 * @args rules_vec: a vector of rule IDs to be deleted
 * @return a space-separated list of deleted internal rule IDs upon success, otherwise an empty string
 */
String
FlowCache::delete_rules_by_internal_id(const Vector<String> &rules_vec)
{
    if (rules_vec.empty()) {
        _errh->error("Flow Cache (port %u): No flow rule mappings to delete", get_port_id());
        return "";
    }

    uint32_t rules_to_delete = rules_vec.size();
    uint32_t internal_rule_ids[rules_to_delete];
    String rule_ids_str = "";
    uint32_t deleted_rules = 0;

    auto it = rules_vec.begin();
    while (it != rules_vec.end()) {
        uint32_t int_rule_id = atoi(it->c_str());
        long rule_id = global_from_internal_rule_id(int_rule_id);
        if (delete_rule_by_global_id(rule_id) < 0) {
            _errh->error("Flow Cache (port %u): Unable to delete rule's %ld mapping", get_port_id(), rule_id);
            return rule_ids_str;
        }

        internal_rule_ids[deleted_rules++] = int_rule_id;
        rule_ids_str += String(int_rule_id) + " ";

        it++;
    }

    if (_verbose) {
        _errh->message("Flow Cache (port %u): Deleted mappings for %" PRIu32 "/%" PRIu32 " rules", get_port_id(), deleted_rules, rules_to_delete);
    }

    return rule_ids_str.trim_space();
}

/**
 * Deletes all rules from this flow cache.
 *
 * @return the number of deleted rules upon success, otherwise a negative integer
 */
int32_t
FlowCache::flush_rules_from_cache()
{
    if (!has_rules()) {
        return 0;
    }

    int32_t flushed_rules_nb = 0;

    auto it = _rules.begin();
    while (it != _rules.end()) {
        HashMap<long, String> *rules_map = it.value();

        if (!rules_map || rules_map->empty()) {
            it++;
            continue;
        }

        flushed_rules_nb += (int32_t) rules_map->size();

        rules_map->clear();
        delete rules_map;

        it++;
    }

    _rules.clear();
    _internal_rule_map.clear();
    flush_rule_counters();

    if ((flushed_rules_nb > 0) && (_verbose)) {
        _errh->message("Flow Cache (port %u): Successfully deleted %" PRId32 " rules from flow cache", get_port_id(), flushed_rules_nb);
    }

    assert(get_rule_counter() == 0);

    return flushed_rules_nb;
}

/**
 * Sets a flow rule's packet counter.
 *
 * @args rule_id: a rule ID
 * @args value: a value to set the counter with
 */
void
FlowCache::set_matched_packets(const uint32_t &rule_id, uint64_t value)
{
    if (rule_id < 0) {
        _errh->error("Flow Cache (port %u): Cannot update packet counters of invalid rule ID %ld", get_port_id(), rule_id);
    }
    _matched_pkts[rule_id] = value;
}

/**
 * Gets the packet counter of a flow rule.
 *
 * @args rule_id: a rule ID
 * @return packet counter of flow rule
 */
uint64_t
FlowCache::get_matched_packets(const uint32_t &rule_id)
{
    if (rule_id < 0) {
        _errh->error("Flow Cache (port %u): No packet counters for invalid rule ID %ld", get_port_id(), rule_id);
    }
    return _matched_pkts[rule_id];
}

/**
 * Sets a flow rule's byte counter.
 *
 * @args rule_id: a rule ID
 * @args value: a value to set the counter with
 */
void
FlowCache::set_matched_bytes(const uint32_t &rule_id, uint64_t value)
{
    if (rule_id < 0) {
        _errh->error("Flow Cache (port %u): Cannot update byte counters of invalid rule ID %ld", get_port_id(), rule_id);
    }
    _matched_bytes[rule_id] = value;
}

/**
 * Gets the byte counter of a flow rule.
 *
 * @args rule_id: a rule ID
 * @return byte counter of flow rule
 */
uint64_t
FlowCache::get_matched_bytes(const uint32_t &rule_id)
{
    if (rule_id < 0) {
        _errh->error("Flow Cache (port %u): No byte counters for invalid rule ID %ld", get_port_id(), rule_id);
    }
    return _matched_bytes[rule_id];
}

/**
 * Initializes a set of rules in the packet and byte counters' memory,
 * increments the rule counter and the next rule ID accordingly.
 *
 * @args rule_ids: an array of rule IDs to be initialized
 * @args rules_nb: the number of rules to be initialized
 */
void
FlowCache::initialize_rule_counters(uint32_t *rule_ids, const uint32_t &rules_nb)
{
    if (!rule_ids || (rules_nb <= 0)) {
        _errh->error("Flow Cache (port %u): Cannot initialize flow counters; no rule IDs provided", get_port_id());
        return;
    }

    for (uint32_t i = 0; i < rules_nb; i++) {
        _matched_pkts[rule_ids[i]] = 0;
        _matched_bytes[rule_ids[i]] = 0;
        if (_verbose) {
            _errh->message("Flow Cache (port %u): Initialized counters for rule with internal ID %" PRIu32, get_port_id(), rule_ids[i]);
        }
    }

    _rules_nb += rules_nb;
    _next_rule_id += rules_nb;
}

/**
 * Deletes a set of rules from the packet and byte counters' memory
 * and decrements the rule counter accordingly.
 *
 * @args rule_ids: an array of rule IDs to be removed
 * @args rules_nb: the number of rules to be removed
 */
void
FlowCache::delete_rule_counters(uint32_t *rule_ids, const uint32_t &rules_nb)
{
    if (!rule_ids || (rules_nb <= 0)) {
        _errh->error("Flow Cache (port %u): Cannot delete flow counters; no rule IDs provided", get_port_id());
        return;
    }

    for (uint32_t i = 0; i < rules_nb; i++) {
        _matched_pkts.erase(rule_ids[i]);
        _matched_bytes.erase(rule_ids[i]);
        if (_verbose) {
            _errh->message("Flow Cache (port %u): Deleted counters for rule with internal ID %" PRIu32, get_port_id(), rule_ids[i]);
        }
    }

    _rules_nb -= rules_nb;
}

/**
 * Resets the flow rule counters of this flow cache.
 */
void
FlowCache::flush_rule_counters()
{
    _rules_nb = 0;
    _next_rule_id = 0;
    _matched_pkts.clear();
    _matched_bytes.clear();
}

/**
 * Flow Director implementation.
 */
// DPDKDevice mode is Flow Director
String FlowDirector::FLOW_DIR_MODE = "flow_dir";

// Supported flow director handlers (called from FromDPDKDevice)
String FlowDirector::FLOW_RULE_ADD         = "rule_add";
String FlowDirector::FLOW_RULE_DEL         = "rules_del";
String FlowDirector::FLOW_RULE_IDS_GLB     = "rules_ids_global";
String FlowDirector::FLOW_RULE_IDS_INT     = "rules_ids_internal";
String FlowDirector::FLOW_RULE_PACKET_HITS = "rule_packet_hits";
String FlowDirector::FLOW_RULE_BYTE_COUNT  = "rule_byte_count";
String FlowDirector::FLOW_RULE_STATS       = "rules_stats";
String FlowDirector::FLOW_RULE_AGGR_STATS  = "rules_aggr_stats";
String FlowDirector::FLOW_RULE_LIST        = "rules_list";
String FlowDirector::FLOW_RULE_COUNT       = "rules_count";
String FlowDirector::FLOW_RULE_FLUSH       = "rules_flush";

// Set of flow rule items supported by the Flow API
HashMap<int, String> FlowDirector::flow_item;

// Set of flow rule actions supported by the Flow API
HashMap<int, String> FlowDirector::flow_action;

// Default verbosity setting
bool FlowDirector::DEF_VERBOSITY = false;

// Global table of DPDK ports mapped to their Flow Director objects
HashTable<portid_t, FlowDirector *> FlowDirector::dev_flow_dir;

// A unique parser
struct cmdline *FlowDirector::_parser = NULL;

// Error handling
int FlowDirector::ERROR = -1;
int FlowDirector::SUCCESS = 0;

FlowDirector::FlowDirector() :
        _port_id(-1), _active(false), _verbose(DEF_VERBOSITY), _rules_filename("")
{
    _errh = new ErrorVeneer(ErrorHandler::default_handler());
    _flow_cache = 0;
}

FlowDirector::FlowDirector(portid_t port_id, ErrorHandler *errh) :
        _port_id(port_id), _active(false), _verbose(DEF_VERBOSITY), _rules_filename("")
{
    _errh = new ErrorVeneer(errh);
    _flow_cache = new FlowCache(port_id, _verbose, _errh);

    populate_supported_flow_items_and_actions();

    if (verbose()) {
        _errh->message("Flow Director (port %u): Created (state %s)", _port_id, _active ? "active" : "inactive");
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
            _errh->message("Flow Director (port %u): Parser deleted", _port_id);
        }
    }

    if (_flow_cache) {
        delete _flow_cache;
    }

    flow_item.clear();
    flow_action.clear();

    if (verbose()) {
        _errh->message("Flow Director (port %u): Destroyed", _port_id);
    }

    delete_error_handler();
}

void
FlowDirector::populate_supported_flow_items_and_actions()
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
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_SET_MPLS_TTL, "OF_SET_MPLS_TTL");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_DEC_MPLS_TTL, "OF_DEC_MPLS_TTL");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_SET_NW_TTL, "OF_SET_NW_TTL");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_DEC_NW_TTL, "OF_DEC_NW_TTL");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_COPY_TTL_OUT, "OF_COPY_TTL_OUT");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_COPY_TTL_IN, "OF_COPY_TTL_IN");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_POP_VLAN, "OF_POP_VLAN");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_PUSH_VLAN, "OF_PUSH_VLAN");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_SET_VLAN_VID, "OF_SET_VLAN_VID");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_SET_VLAN_PCP, "OF_SET_VLAN_PCP");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_POP_MPLS, "OF_POP_MPLS");
    flow_action.insert((int) RTE_FLOW_ACTION_TYPE_OF_PUSH_MPLS, "OF_PUSH_MPLS");
#endif
}

/**
 * Obtains an instance of the Flow Director parser.
 *
 * @args errh: an instance of the error handler
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
 * Obtains the flow cache associated with this Flow Director.
 *
 * @return a Flow Cache object
 */
FlowCache *
FlowDirector::get_flow_cache()
{
    return _flow_cache;
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
    return dev_flow_dir;
}

/**
 * Cleans the global map of DPDK ports to
 * their Flow Director instances.
 */
void
FlowDirector::clean_flow_director_map()
{
    if (!dev_flow_dir.empty()) {
        dev_flow_dir.clear();
    }
}

/**
 * Manages the Flow Director instances.
 *
 * @args port_id: the ID of the NIC
 * @args errh: an instance of the error handler
 * @return a Flow Director object for this NIC
 */
FlowDirector *
FlowDirector::get_flow_director(const portid_t &port_id, ErrorHandler *errh)
{
    if (!errh) {
        errh = ErrorHandler::default_handler();
    }

    // Invalid port ID
    if (port_id >= DPDKDevice::dev_count()) {
        click_chatter("Flow Director (port %u): Denied to create instance for invalid port", port_id);
        return NULL;
    }

    // Get the Flow Director of the desired port
    FlowDirector *flow_dir = dev_flow_dir.get(port_id);

    // Not there, let's created it
    if (!flow_dir) {
        flow_dir = new FlowDirector(port_id, errh);
        assert(flow_dir);
        dev_flow_dir[port_id] = flow_dir;
    }

    // Create a Flow Director parser
    _parser = parser(errh);

    // Ship it back
    return flow_dir;
}

/**
 * Installs a set of string-based rules read from a file.
 *
 * @args filename: the file that contains the rules
 * @return the number of rules being installed, otherwise a negative integer
 */
int32_t
FlowDirector::add_rules_from_file(const String &filename)
{
    if (filename.empty()) {
        return _errh->warning("Flow Director (port %u): No file provided", _port_id);
    }

    FILE *fp = NULL;
    fp = fopen(filename.c_str(), "r");
    if (fp == NULL) {
        return _errh->error("Flow Director (port %u): Failed to open file '%s'", _port_id, filename.c_str());
    }

    uint32_t rules_nb = 0;
    uint32_t installed_rules_nb = 0;

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
            return _errh->error("Flow Director (port %u): Invalid rule '%s'", _port_id, line);
        }

        // Compose rule for the right NIC
        String rule = "flow create " + String(_port_id) + " " + String(line);

        // Parse the queue index to infer the CPU core
        String queue_index_str = fetch_token_after_keyword((char *) rule.c_str(), "queue index");
        int core_id = atoi(queue_index_str.c_str());

        if (_verbose) {
            _errh->message("Flow Director (port %u): About to install rule #%" PRIu32 ": %s", _port_id, rules_nb, line);
        }

        uint32_t next_int_rule_id = _flow_cache->next_internal_rule_id();

        if (flow_rule_install(next_int_rule_id, rule, (long) next_int_rule_id, core_id) == SUCCESS) {
            installed_rules_nb++;
        }
    }

    // Close the file
    fclose(fp);

    _errh->message("Flow Director (port %u): %" PRIu32 "/%" PRIu32 " rules are installed", _port_id, flow_rules_count(), rules_nb);

    return (int32_t) installed_rules_nb;
}

/**
 * Translates a string-based rule into a flow rule
 * object and installs it in a NIC.
 * If no global rule and CPU core IDs are given ,then
 * we return without interacting with Flow Cache
 * (the caller must have done this already).
 *
 * @args int_rule_id: a flow rule's internal ID
 * @args rule: a flow rule as a string
 * @args rule_id: a flow rule's global ID (optional)
 * @args core_id: a CPU core ID associated with this flow rule (optional)
 * @return a flow rule object
 */
int
FlowDirector::flow_rule_install(const uint32_t &int_rule_id, const String &rule, long rule_id, int core_id)
{
    // Only active instances can configure a NIC
    if (!active()) {
        _errh->error("Flow Director (port %u): Inactive instance cannot install rule #%" PRIu32, _port_id, int_rule_id);
        return ERROR;
    }

    // TODO: Fix DPDK to return proper status
    int res = flow_parser_parse(_parser, (char *) rule.c_str(), _errh);
    if (res >= 0) {
        // Some parameters must be inferred
        if ((rule_id < 0) && (core_id < 0)) {
            return SUCCESS;
        }

        // Update flow cache
        if (_flow_cache->update_rule_in_flow_cache(core_id, rule_id, int_rule_id, rule)) {
            if (_verbose) {
                _errh->message("Flow Director (port %u): Successfully installed rule with internal ID %" PRIu32, _port_id, int_rule_id);
            }
            return SUCCESS;
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

    _errh->error("Flow Director (port %u): Failed to parse rule '%s' due to %s", _port_id, rule.c_str(), error.c_str());

    return ERROR;
}

/**
 * Returns a flow rule object of a specific NIC with specific rule ID.
 *
 * @args int_rule_id: an internal rule ID
 * @return a flow rule object
 */
struct port_flow *
FlowDirector::flow_rule_get(const uint32_t &int_rule_id)
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
 * Removes a batch of flow rule objects from a NIC.
 *
 * @args int_rule_ids: an array of internal flow rule IDs
 * @args rules_nb: the number of rules to delete
 * @return status
 */
int
FlowDirector::flow_rules_delete(uint32_t *int_rule_ids, const uint32_t &rules_nb, const bool with_cache)
{
    // Only active instances can configure a NIC
    if (!active()) {
        return _errh->error("Flow Director (port %u): Inactive instance cannot remove rules", _port_id);
    }

    // Inputs' sanity check
    if ((!int_rule_ids) || (rules_nb == 0)) {
        return _errh->error("Flow Director (port %u): No rules to remove", _port_id);
    }

    // TODO: For N rules, port_flow_destroy calls rte_flow_destroy N times.
    //       DPDK must act upon this.
    if (port_flow_destroy(_port_id, rules_nb, int_rule_ids) != FLOWDIR_SUCCESS) {
        return _errh->error(
            "Flow Director (port %u): Failed to remove a batch of %" PRIu32 " rules",
            _port_id, rules_nb
        );
    }

    // Update flow cache
    if (with_cache) {
        String rule_ids_str = _flow_cache->delete_rules_by_internal_id(int_rule_ids, rules_nb);
        if (rule_ids_str.empty()) {
            return ERROR;
        }
    }

    if (_verbose) {
        _errh->message("Flow Director (port %u): Successfully deleted %" PRIu32 " rules", _port_id, rules_nb);
    }

    return SUCCESS;
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
FlowDirector::flow_rule_query(const uint32_t &int_rule_id, int64_t &matched_pkts, int64_t &matched_bytes)
{
    // Only active instances can query a NIC
    if (!active()) {
        _errh->error(
            "Flow Director (port %u): Inactive instance cannot query flow rule #%" PRIu32,
            _port_id, int_rule_id
        );
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
        if (pf->id == int_rule_id) {
            action = pf->actions;
            break;
        }
    }
    if (!pf || !action) {
        _errh->message(
            "Flow Director (port %u): No stats for invalid flow rule with ID %" PRIu32,
            _port_id, int_rule_id
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
            "Flow Director (port %u): No count instruction for flow rule with ID %" PRIu32,
            _port_id, int_rule_id
        );
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
            "Flow Director (port %u): Failed to query stats for flow rule with ID %" PRIu32,
            _port_id, int_rule_id
        );
        return "";
    }

    if (query.hits_set == 1) {
        _flow_cache->set_matched_packets(int_rule_id, query.hits);
        matched_pkts = query.hits;
    }
    if (query.bytes_set == 1) {
        _flow_cache->set_matched_bytes(int_rule_id, query.bytes);
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
 * Reports aggregate flow rule statistics on a NIC.
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
        pkts_per_queue[(uint16_t) queue] += _flow_cache->get_matched_packets(pf->id);
        bytes_per_queue[(uint16_t) queue] += _flow_cache->get_matched_bytes(pf->id);
        tot_pkts += _flow_cache->get_matched_packets(pf->id);
        tot_bytes += _flow_cache->get_matched_bytes(pf->id);
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
    aggr_stats << "Bytes imbalance ratio over "  << queues_nb << " queues: " << byte_imbalance_ratio;

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
    return _flow_cache->get_rule_counter();
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
            _errh->message("Flow Director (port %u): No flow rules to count", _port_id);
        }
        return 0;
    }

    uint32_t rules_nb = 0;

    // Traverse the list of installed flow rules
    for (struct port_flow *pf = port->flow_list; pf != NULL; pf = pf->next) {
        rules_nb++;
    }

    // Consistency
    assert(flow_rules_count() == rules_nb);

    return rules_nb;
}

/**
 * Lists all of a NIC's rules.
 *
 * @return a string of NIC flow rules (each on a different line)
 */
String
FlowDirector::flow_rules_list()
{
    if (!active()) {
        return "Flow Director is inactive";
    }

    struct rte_port *port = get_port(_port_id);
    if (!port->flow_list || (flow_rules_count() == 0)) {
        _errh->error("Flow Director (port %u): No flow rules to list", _port_id);
        return "No flow rules";
    }

    // Sort flows by group, priority, and ID
    struct port_flow *sorted_rules = NULL;
    flow_rules_sort(port, &sorted_rules);

    StringAccum rules_list;

    // Traverse and print the sorted list of installed flow rules
    for (struct port_flow *pf = sorted_rules; pf != NULL; pf = pf->tmp) {
        const struct rte_flow_item *item = pf->pattern;
        const struct rte_flow_action *action = pf->actions;

        rules_list << "Flow rule #" << pf->id << ": [";
        rules_list << "Group: " << pf->attr.group << ", Prio: " << pf->attr.priority << ", ";
        rules_list << "Scope: " << (pf->attr.ingress == 1 ? "ingress" : "-");
        rules_list << "/" << (pf->attr.egress == 1 ? "egress" : "-");
    #if RTE_VERSION >= RTE_VERSION_NUM(18,5,0,0)
        rules_list << "/" << (pf->attr.transfer == 1 ? "transfer" : "-");
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
        if (_flow_cache->internal_rule_id_exists(pf->id)) {
            rules_list << ", ";
            rules_list << "Stats: ";
            rules_list << "Matched packets: " << _flow_cache->get_matched_packets(pf->id) << ", ";
            rules_list << "Matched bytes: " << _flow_cache->get_matched_bytes(pf->id);
        }

        rules_list << "]\n";
    }

    if (rules_list.empty()) {
        rules_list << "No flow rules";
    }

    return rules_list.take_string();
}

/**
 * Lists all of a NIC's internal rule IDs.
 *
 * @return a string of space-separated internal flow rule IDs, otherwise a relevant message
 */
String
FlowDirector::flow_rule_ids_internal()
{
    if (!active()) {
        return "Flow Director is inactive";
    }

    struct rte_port *port = get_port(_port_id);
    if (!port->flow_list || (flow_rules_count() == 0)) {
        _errh->error("Flow Director (port %u): No flow rule IDs to list", _port_id);
        return "No flow rules";
    }

    struct port_flow *list = NULL;

    // Sort flows by group, priority, and ID
    struct port_flow *sorted_rules = NULL;
    flow_rules_sort(port, &sorted_rules);

    StringAccum rule_ids;

    // Traverse the sorted list of installed flow rules and keep their IDs
    for (struct port_flow *pf = sorted_rules; pf != NULL; pf = pf->tmp) {
        rule_ids << pf->id << " ";
    }

    if (rule_ids.empty()) {
        rule_ids << "No flow rules";
    }

    return rule_ids.take_string().trim_space();
}

/**
 * Lists all of a NIC's global rule IDs.
 *
 * @return a string of space-separated global flow rule IDs
 */
String
FlowDirector::flow_rule_ids_global()
{
    if (!active()) {
        return "Flow Director is inactive";
    }

    return _flow_cache->global_rule_ids();
}

/**
 * Sorts a list of flow rules by group, priority, and ID.
 *
 * @args port: flow rule list to sort
 * @return a sorted flow rule list
 */
void
FlowDirector::flow_rules_sort(struct rte_port *port, struct port_flow **sorted_rules)
{
    if (!port || !port->flow_list) {
        _errh->error("Flow Director (port %u): Cannot sort empty flow rules' list", _port_id);
        return;
    }

    for (struct port_flow *pf = port->flow_list; pf != NULL; pf = pf->next) {
        struct port_flow **tmp;

        tmp = sorted_rules;
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
}

/**
 * Flushes all of the rules from a NIC associated with this Flow Director instance.
 *
 * @return the number of rules being flushed
 */
uint32_t
FlowDirector::flow_rules_flush()
{
    // Only active instances can configure a NIC
    if (!active()) {
        _errh->message("Flow Director (port %u): Nothing to flush", _port_id);
        return 0;
    }

    uint32_t rules_before_flush = flow_rules_count_explicit();
    if (rules_before_flush == 0) {
        return 0;
    }

    // Successful flush means zero rules left
    if (port_flow_flush(_port_id) == FLOWDIR_SUCCESS) {
        _flow_cache->flush_rules_from_cache();
        return rules_before_flush;
    }

    // Now, count again to verify what is left
    return flow_rules_count_explicit();
}

/**
 * Filters unwanted components from rule and returns an updated rule by reference.
 * If a rule is composed for the wrong port ID, this method strips it off and later
 * flow_rule_install prepends a correct port ID.
 *
 * @args rule: a rule to filter
 * @return boolean status
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

/**
 * Returns a rule token after an input keyword.
 *
 * @args rule: a rule to parse
 * @args keyword: a keyword to match before the token
 * @return rule token after keyword upon success, otherwise empty string
 */
String
FlowDirector::fetch_token_after_keyword(char *rule, const String &keyword)
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

#endif /* RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0) */

CLICK_ENDDECLS
