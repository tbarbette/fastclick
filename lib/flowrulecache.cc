// -*- c-basic-offset: 4; related-file-name: "flowrulecache.hh" -*-
/*
 * flowrulecache.cc -- Implementation of a flow rule cache for DPDK's Flow API
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
#include <click/flowrulecache.hh>

CLICK_DECLS

#if RTE_VERSION >= RTE_VERSION_NUM(20,2,0,0)

/**
 * Flow Rule Cache implementation.
 */

/**
 * Return the NIC port ID associated with this flow rule cache.
 *
 * @return port ID of the NIC associated with this cache
 */
portid_t
FlowRuleCache::get_port_id()
{
    return _port_id;
}

/**
 * Return the NIC device's adress suitable to use as a FromDPDKDevice reference.
 *
 * @return port address of the NIC associated with this cache
 */
String
FlowRuleCache::get_device_address()
{
    // TODO: Returning the PCI address would be better
    // return String(_fd->get_device()->get_device_id());
    return String(_port_id);
}

/**
 * Checks whether this flow rule cache contains any flow rules or not.
 *
 * @return true if at least one flow rule is in the cache, otherwise false
 */
bool
FlowRuleCache::has_rules()
{
    return !_rules.empty();
}

/**
 * Checks whether a given global flow rule ID is present or not.
 *
 * @args rule_id: a global flow rule ID to find
 * @return true if rule_id exists, otherwise false
 */
bool
FlowRuleCache::global_rule_id_exists(const uint32_t &rule_id)
{
    return (internal_from_global_rule_id(rule_id) >= 0) ? true : false;
}

/**
 * Checks whether an internal flow rule ID is present or not.
 *
 * @args int_rule_id: an internal flow rule ID to find
 * @return true if int_rule_id exists, otherwise false
 */
bool
FlowRuleCache::internal_rule_id_exists(const uint32_t &int_rule_id)
{
    return (global_from_internal_rule_id(int_rule_id) >= 0) ? true : false;
}

/**
 * Traverses the map of global flow rule IDs to internal NIC IDs,
 * and checks whether an internal flow rule ID is present or not.
 *
 * @args int_rule_id: an internal flow rule ID to find
 * @return the global flow rule ID mapped to the input internal flow rule ID upon success,
 *         otherwise a negative integer
 */
uint32_t
FlowRuleCache::global_from_internal_rule_id(const uint32_t &int_rule_id)
{
    if (int_rule_id < 0) {
        return (uint32_t) _errh->error("Flow Rule Cache (port %u): Unable to verify mapping due to invalid internal NIC rule ID %" PRIu32, get_port_id(), int_rule_id);
    }

    auto it = _internal_rule_map.begin();
    while (it != _internal_rule_map.end()) {
        uint32_t r_id = it.key();
        uint32_t int_r_id = it.value();

        if (int_r_id == int_rule_id) {
            // if (_verbose) {
            //     _errh->message("Flow Rule Cache (port %u): Internal rule ID %" PRIu32 " is mapped to global rule ID %ld", get_port_id(), int_rule_id, r_id);
            // }
            return r_id;
        }

        it++;
    }

    if (_verbose) {
        _errh->message("Flow Rule Cache (port %u): Internal rule ID %" PRIu32 " does not exist in the flow rule cache", get_port_id(), int_rule_id);
    }

    return (uint32_t) FLOWRULEPARSER_ERROR;
}

/**
 * Queries the map of global flow rule IDs to internal NIC IDs,
 * and checks whether a global flow rule ID is present or not.
 *
 * @args rule_id: a global flow rule ID to find
 * @return the internal flow rule ID mapped to the input global flow rule ID upon success,
 *         otherwise a negative integer
 */
int32_t
FlowRuleCache::internal_from_global_rule_id(const uint32_t &rule_id)
{
    if (rule_id < 0) {
        return (int32_t) _errh->error("Flow Rule Cache (port %u): Unable to verify mapping due to invalid global NIC rule ID %" PRIu32, get_port_id(), rule_id);
    }

    const uint32_t *found = _internal_rule_map.findp(rule_id);
    if (!found) {
        if (_verbose) {
            _errh->message("Flow Rule Cache (port %u): Global rule ID %" PRIu32 " does not exist in the flow rule cache", get_port_id(), rule_id);
        }
        return (int32_t) FLOWRULEPARSER_ERROR;
    }

    // if (_verbose) {
    //     _errh->message("Flow Rule Cache (port %u): Global rule ID %" PRIu32 " is mapped to internal rule ID %" PRIu32, get_port_id(), rule_id, *found);
    // }

    return (int32_t) *found;
}

/**
 * Returns the list of global flow rules IDs in this flow rule cache.
 *
 * @arg increasing: boolean flag that indicates the order of rule IDs (defaults to true)
 * @return list of global flow rules IDs
 */
Vector<uint32_t>
FlowRuleCache::global_rule_ids(const bool increasing)
{
    Vector<uint32_t> rule_ids;

    auto ext_it = _rules.begin();
    while (ext_it != _rules.end()) {
        HashMap<uint32_t, String> *rules_map = ext_it.value();

        // No rules associated with this CPU core
        if (!rules_map || rules_map->empty()) {
            ext_it++;
            continue;
        }

        auto it = rules_map->begin();
        while (it != rules_map->end()) {
            uint32_t rule_id = it.key();
            rule_ids.push_back(rule_id);

            it++;
        }

        ext_it++;
    }

    if (increasing) {
        sort_rule_ids_inc(rule_ids);
    } else {
        sort_rule_ids_dec(rule_ids);
    }

    return rule_ids;
}

/**
 * Returns the list of internal flow rules IDs in this flow rule cache.
 *
 * @arg increasing: boolean flag that indicates the order of rule IDs (defaults to true)
 * @return list of internal flow rules IDs
 */
Vector<uint32_t>
FlowRuleCache::internal_rule_ids(const bool increasing)
{
    Vector<uint32_t> rule_ids;

    auto it = _internal_rule_map.begin();
    while (it != _internal_rule_map.end()) {
        uint32_t int_r_id = it.value();
        rule_ids.push_back(int_r_id);
        it++;
    }

    if (increasing) {
        sort_rule_ids_inc(rule_ids);
    } else {
        sort_rule_ids_dec(rule_ids);
    }

    return rule_ids;
}

/**
 * Returns the list of internal flow rules IDs with counters.
 *
 * @arg increasing: boolean flag that indicates the order of rule IDs (defaults to true)
 * @return list of internal flow rules IDs with counters
 */
Vector<uint32_t>
FlowRuleCache::internal_rule_ids_counters(const bool increasing)
{
    Vector<uint32_t> rule_ids;

    auto it = _matched_pkts.begin();
    while (it != _matched_pkts.end()) {
        uint32_t int_r_id = it.key();
        rule_ids.push_back(int_r_id);
        it++;
    }

    if (increasing) {
        sort_rule_ids_inc(rule_ids);
    } else {
        sort_rule_ids_dec(rule_ids);
    }

    return rule_ids;
}

/**
 * Returns a map of flow rule IDs to flow rules.
 *
 * @args core_id: a CPU core ID
 * @return a map of flow rule IDs to flow rules associated with this core
 */
HashMap<uint32_t, String> *
FlowRuleCache::rules_map_by_core_id(const int &core_id)
{
    if (core_id < 0) {
        _errh->error("Flow Rule Cache (port %u): Unable to find rule map due to invalid CPU core ID %d", get_port_id(), core_id);
        return NULL;
    }

    return _rules.find(core_id);
}

/**
 * Returns a list of flow rules associated with a CPU core.
 *
 * @args core_id: a CPU core ID
 * @return a list of flow rules associated with this core
 */
Vector<String>
FlowRuleCache::rules_list_by_core_id(const int &core_id)
{
    Vector<String> rules_vec;

    if (core_id < 0) {
        _errh->error("Flow Rule Cache (port %u): Unable to find rules due to invalid CPU core ID %d", get_port_id(), core_id);
        return rules_vec;
    }

    HashMap<uint32_t, String> *rules_map = rules_map_by_core_id(core_id);
    if (!rules_map) {
        _errh->error("Flow Rule Cache (port %u): No rules associated with CPU core ID %d", get_port_id(), core_id);
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
 * Returns the number of CPU cores that have at least one flow rule each.
 *
 * @return a list CPU cores that have associated flow rules
 */
Vector<int>
FlowRuleCache::cores_with_rules()
{
    Vector<int> cores_with_rules;

    for (unsigned i = 0; i < click_max_cpu_ids(); i++) {
        Vector<String> core_rules = rules_list_by_core_id(i);

        if (core_rules.empty()) {
            continue;
        }

        cores_with_rules[i] = i;
    }

    return cores_with_rules;
}

/**
 * Returns a flow rule by its global flow rule ID.
 *
 * @arg rule_id: the global flow rule ID of the rule to retrieve
 * @return a rule upon success, otherwise empty string
 */
String
FlowRuleCache::get_rule_by_global_id(const uint32_t &rule_id)
{
    if (rule_id < 0) {
        _errh->error("Flow Rule Cache (port %u): Unable to print rule with invalid rule ID %" PRIu32, get_port_id(), rule_id);
        return "";
    }

    auto it = _rules.begin();
    while (it != _rules.end()) {
        int core_id = it.key();
        HashMap<uint32_t, String> *rules_map = it.value();

        // No rules associated with this CPU core
        if (!rules_map || rules_map->empty()) {
            it++;
            continue;
        }

        String *rule = rules_map->findp(rule_id);
        // This rule ID does not belong to this CPU core
        if (!rule) {
            it++;
            continue;
        }

        // Found
        return *rule;
    }

    return "";
}

/**
 * Returns a flow rule by its global flow rule ID.
 *
 * @arg int_rule_id: the internal flow rule ID of the rule to retrieve
 * @return a rule upon success, otherwise empty string
 */
String
FlowRuleCache::get_rule_by_internal_id(const uint32_t &int_rule_id)
{
    return get_rule_by_global_id(global_from_internal_rule_id(int_rule_id));
}

/**
 * Keeps a mapping between global and internal NIC flow rule IDs.
 *
 * @args rule_id: a global flow rule ID
 * @args int_rule_id: an internal flow rule ID mapped to rule_id
 * @return boolean status
 */
bool
FlowRuleCache::store_rule_id_mapping(const uint32_t &rule_id, const uint32_t &int_rule_id)
{
    if (rule_id < 0) {
        _errh->error("Flow Rule Cache (port %u): Unable to store mapping due to invalid rule ID %" PRIu32, get_port_id(), rule_id);
        return false;
    }

    if (!_internal_rule_map.insert(rule_id, int_rule_id)) {
        _errh->error("Flow Rule Cache (port %u): Failed to inserted rule mapping %" PRIu32 " <--> %" PRIu32, get_port_id(), rule_id, int_rule_id);
        return false;
    }

    if (_verbose) {
        _errh->message("Flow Rule Cache (port %u): Successfully inserted rule mapping %" PRIu32 " <--> %" PRIu32, get_port_id(), rule_id, int_rule_id);
    }

    return true;
}

/**
 * Deletes a mapping between a global and an internal NIC flow rule IDs.
 *
 * @args rule_id: a global flow rule ID to delete its mapping
 * @return boolean status
 */
bool
FlowRuleCache::delete_rule_id_mapping(const uint32_t &rule_id)
{
    if (rule_id < 0) {
        _errh->error("Flow Rule Cache (port %u): Unable to delete mapping for invalid rule ID %" PRIu32, get_port_id(), rule_id);
        return false;
    }

    if (_internal_rule_map.remove(rule_id)) {
        if (_verbose) {
            _errh->message("Flow Rule Cache (port %u): Successfully deleted mapping for rule ID %" PRIu32, get_port_id(), rule_id);
        }

        return true;
    }

    return false;
}

/**
 * Returns the currently maximum internal flow rule ID.
 *
 * @return currently maximum internal flow rule ID
 */
int32_t
FlowRuleCache::currently_max_internal_rule_id()
{
    return _next_rule_id - 1;
}

/**
 * Returns the next internal flow rule ID to use for insertion.
 *
 * @return next available internal flow rule ID
 */
uint32_t
FlowRuleCache::next_internal_rule_id()
{
    return _next_rule_id++;
}

/**
 * Sets the next internal flow rule ID to use for insertion.
 *
 * @arg next_id: next available internal flow rule ID
 */
void
FlowRuleCache::set_next_internal_rule_id(uint32_t next_id)
{
    _next_rule_id = next_id;
}

/**
 * Adds a new flow rule to this flow rule cache.
 *
 * @args core_id: a CPU core ID associated with the flow rule
 * @args rule_id: a global rule ID associated with the flow rule
 * @args int_rule_id: an internal flow rule ID associated with the flow rule
 * @args rule: the actual flow rule
 * @return 0 upon success, otherwise a negative integer
 */
int
FlowRuleCache::insert_rule_in_flow_cache(const int &core_id, const uint32_t &rule_id, const uint32_t &int_rule_id, const String rule)
{
    if (core_id < 0) {
        return _errh->error("Flow Rule Cache (port %u): Unable to add rule due to invalid CPU core ID %d", get_port_id(), core_id);
    }

    if (rule_id < 0) {
        return _errh->error("Flow Rule Cache (port %u): Unable to add rule due to invalid rule ID %" PRIu32, get_port_id(), rule_id);
    }

    if (int_rule_id < 0) {
        return _errh->error("Flow Rule Cache (port %u): Unable to add rule due to invalid internal rule ID %" PRIu32, get_port_id(), int_rule_id);
    }

    if (rule.empty()) {
        return _errh->error("Flow Rule Cache (port %u): Unable to add rule due to empty input", get_port_id());
    }

    HashMap<uint32_t, String> *rules_map = rules_map_by_core_id(core_id);
    if (!rules_map) {
        _rules.insert(core_id, new HashMap<uint32_t, String>());
        rules_map = rules_map_by_core_id(core_id);
        assert(rules_map);
    }

    if (!rules_map->insert(rule_id, rule)) {
        return _errh->error("Flow Rule Cache (port %u): Unable to add rule due to cache failure", get_port_id());
    }

    if (!store_rule_id_mapping(rule_id, int_rule_id)) {
        return _errh->error("Flow Rule Cache (port %u): Unable to add rule mapping due to cache failure", get_port_id());
    }

    if (_verbose) {
        _errh->message(
            "Flow Rule Cache (port %u): Rule %" PRIu32 " added and mapped with internal rule ID %" PRIu32 " and queue %d",
            get_port_id(), rule_id, int_rule_id, core_id
        );
    }

    return FLOWRULEPARSER_SUCCESS;
}

/**
 * Updates a flow rule using a two-phase commit.
 * First checks if flow rule exists and if so deletes it.
 * Then, inserts the new flow rule.
 *
 * @args core_id: a new CPU core ID associated with this flow rule
 * @args rule_id: the global flow rule ID of the flow rule to be updated
 * @args int_rule_id: a new internal flow rule ID to be associated with this flow rule
 * @args rule: the actual flow rule to be updated
 * @return true upon success, otherwise false
 */
bool
FlowRuleCache::update_rule_in_flow_cache(const int &core_id, const uint32_t &rule_id, const uint32_t &int_rule_id, String rule)
{
    // First try to delete this rule, if it exists
    delete_rule_by_global_id(rule_id);

    // Now, store this new rule in this CPU core's flow rule cache
    return (insert_rule_in_flow_cache(core_id, rule_id, int_rule_id, rule) == FLOWRULEPARSER_SUCCESS);
}

/**
 * Deletes a flow rule from this flow rule cache using its global ID as an index.
 *
 * @args rule_id: the global flow rule ID of the flow rule to be deleted
 * @return the internal flow rule ID being deleted upon success, otherwise a negative integer
 */
int32_t
FlowRuleCache::delete_rule_by_global_id(const uint32_t &rule_id)
{
    if (rule_id < 0) {
        return _errh->error("Flow Rule Cache (port %u): Unable to delete rule due to invalid global rule ID %" PRIu32, get_port_id(), rule_id);
    }

    auto it = _rules.begin();
    while (it != _rules.end()) {
        int core_id = it.key();
        HashMap<uint32_t, String> *rules_map = it.value();

        // No rules associated with this CPU core
        if (!rules_map || rules_map->empty()) {
            it++;
            continue;
        }

        // Remove rule from the cache
        if (rules_map->remove(rule_id)) {
            // Fetch the mapping of the global rule ID with the internal rule ID
            int32_t int_rule_id = internal_from_global_rule_id(rule_id);
            if (int_rule_id < 0) {
                return _errh->error("Flow Rule Cache (port %u): Unable to delete rule %" PRIu32 " due to no internal mapping", get_port_id(), rule_id);
            }

            // Now delete this mapping
            if (!delete_rule_id_mapping(rule_id)) {
                return FLOWRULEPARSER_ERROR;
            }

            if (_verbose) {
                _errh->message(
                    "Flow Rule Cache (port %u): Rule with global ID %" PRIu32 " and internal rule ID %" PRIu32 " deleted from queue %d",
                    get_port_id(), rule_id, (uint32_t) int_rule_id, core_id
                );
            }

            // Update counters
            uint32_t int_rule_ids[1] = {(uint32_t) int_rule_id};
            delete_rule_counters(int_rule_ids, 1);

            return int_rule_id;
        }

        it++;
    }

    if (_verbose) {
        _errh->message("Flow Rule Cache (port %u): Unable to delete rule %" PRIu32 " due to cache miss", get_port_id(), rule_id);
    }

    return FLOWRULEPARSER_ERROR;
}

/**
 * Deletes a list of flow ules from this flow rule cache.
 *
 * @args int_rule_ids: an array of internal flow rule IDs to be deleted
 * @args rules_nb: the number of flow rule IDs to be deleted
 * @return a space-separated list of deleted internal flow rule IDs upon success, otherwise an empty string
 */
String
FlowRuleCache::delete_rules_by_internal_id(const uint32_t *int_rule_ids, const uint32_t &rules_nb)
{
    String int_rule_ids_str = "";
    for (uint32_t i = 0; i < rules_nb; i++) {
        int_rule_ids_str += String(int_rule_ids[i]) + " ";
    }

    return delete_rules_by_internal_id(int_rule_ids_str.trim_space().split(' '));
}

/**
 * Deletes a list of flow rules from this flow rule cache.
 *
 * @args rules_vec: a vector of flow rule IDs to be deleted
 * @return a space-separated list of deleted internal flow rule IDs upon success, otherwise an empty string
 */
String
FlowRuleCache::delete_rules_by_internal_id(const Vector<String> &rules_vec)
{
    if (rules_vec.empty()) {
        _errh->error("Flow Rule Cache (port %u): No flow rule mappings to delete", get_port_id());
        return "";
    }

    uint32_t rules_to_delete = rules_vec.size();
    uint32_t deleted_rules = 0;
    String rule_ids_str = "";

    auto it = rules_vec.begin();
    while (it != rules_vec.end()) {
        uint32_t int_rule_id = atoi(it->c_str());
        uint32_t rule_id = global_from_internal_rule_id(int_rule_id);
        if (rule_id < 0) {
            _errh->error("Flow Rule Cache (port %u): Unable to delete mapping for rule with internal ID %" PRIu32, get_port_id(), int_rule_id);
            return rule_ids_str.trim_space();
        }

        if (delete_rule_by_global_id(rule_id) < 0) {
            _errh->error("Flow Rule Cache (port %u): Unable to delete mapping for rule with global ID %" PRIu32, get_port_id(), rule_id);
            return rule_ids_str.trim_space();
        }

        rule_ids_str += String(int_rule_id) + " ";

        deleted_rules++;
        it++;
    }

    if (_verbose) {
        _errh->message("Flow Rule Cache (port %u): Deleted mappings for %" PRIu32 "/%" PRIu32 " rules", get_port_id(), deleted_rules, rules_to_delete);
    }

    return rule_ids_str.trim_space();
}

/**
 * Deletes all flow rules from this flow rule cache.
 *
 * @return the number of flushed flow rules upon success, otherwise a negative integer
 */
int32_t
FlowRuleCache::flush_rules_from_cache()
{
    if (!has_rules()) {
        return 0;
    }

    int32_t flushed_rules_nb = 0;

    auto it = _rules.begin();
    while (it != _rules.end()) {
        HashMap<uint32_t, String> *rules_map = it.value();

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
        _errh->message("Flow Rule Cache (port %u): Successfully deleted %" PRId32 " rules from flow rule cache", get_port_id(), flushed_rules_nb);
    }

    assert(get_rule_counter() == 0);

    return flushed_rules_nb;
}

/**
 * Sets a flow rule's packet counter.
 *
 * @args int_rule_id: an internal flow rule ID
 * @args value: a value to set the counter
 */
void
FlowRuleCache::set_matched_packets(const uint32_t &int_rule_id, uint64_t value)
{
    if (int_rule_id < 0) {
        _errh->error("Flow Rule Cache (port %u): Cannot update packet counters of invalid rule ID %" PRIu32, get_port_id(), int_rule_id);
    }
    _matched_pkts.insert(int_rule_id, value);
}

/**
 * Gets the packet counter of a flow rule.
 *
 * @args int_rule_id: an internal flow rule ID
 * @return packet counter of flow rule
 */
uint64_t
FlowRuleCache::get_matched_packets(const uint32_t &int_rule_id)
{
    if (int_rule_id < 0) {
        _errh->error("Flow Rule Cache (port %u): No packet counters for invalid rule ID %" PRIu32, get_port_id(), int_rule_id);
    }
    return _matched_pkts.find(int_rule_id);
}

/**
 * Sets a flow rule's byte counter.
 *
 * @args int_rule_id: an internal flow rule ID
 * @args value: a value to set the counter
 */
void
FlowRuleCache::set_matched_bytes(const uint32_t &int_rule_id, uint64_t value)
{
    if (int_rule_id < 0) {
        _errh->error("Flow Rule Cache (port %u): Cannot update byte counters of invalid rule ID %" PRIu32, get_port_id(), int_rule_id);
    }
    _matched_bytes.insert(int_rule_id, value);
}

/**
 * Gets the byte counter of a flow rule.
 *
 * @args int_rule_id: an internal flow rule ID
 * @return byte counter of flow rule
 */
uint64_t
FlowRuleCache::get_matched_bytes(const uint32_t &int_rule_id)
{
    if (int_rule_id < 0) {
        _errh->error("Flow Rule Cache (port %u): No byte counters for invalid rule ID %" PRIu32, get_port_id(), int_rule_id);
    }
    return _matched_bytes.find(int_rule_id);
}

/**
 * Initializes a set of flow rules in the packet and byte counters' memory,
 * increments the flow rule counter and the next flow rule ID accordingly.
 *
 * @args int_rule_ids: an array of internal flow rule IDs to be initialized
 * @args rules_nb: the number of flow rules to be initialized
 */
void
FlowRuleCache::initialize_rule_counters(uint32_t *int_rule_ids, const uint32_t &rules_nb)
{
    if (!int_rule_ids || (rules_nb <= 0)) {
        _errh->error("Flow Rule Cache (port %u): Cannot initialize flow counters; no rule IDs provided", get_port_id());
        return;
    }

    for (uint32_t i = 0; i < rules_nb; i++) {
        _matched_pkts.insert(int_rule_ids[i], 0);
        _matched_bytes.insert(int_rule_ids[i], 0);
        if (_verbose) {
            _errh->message("Flow Rule Cache (port %u): Initialized counters for rule with internal ID %" PRIu32, get_port_id(), int_rule_ids[i]);
        }
    }

    _rules_nb += rules_nb;
}

/**
 * Deletes a set of flow rules from the packet and byte counters' memory
 * and decrements the flow rule counter accordingly.
 *
 * @args int_rule_ids: an array of internal flow rule IDs to be removed
 * @args rules_nb: the number of flow rules to be removed
 */
void
FlowRuleCache::delete_rule_counters(uint32_t *int_rule_ids, const uint32_t &rules_nb)
{
    if (!int_rule_ids || (rules_nb <= 0)) {
        _errh->error("Flow Rule Cache (port %u): Cannot delete flow counters; no rule IDs provided", get_port_id());
        return;
    }

    for (uint32_t i = 0; i < rules_nb; i++) {
        _matched_pkts.erase(int_rule_ids[i]);
        _matched_bytes.erase(int_rule_ids[i]);
        if (_verbose) {
            _errh->message("Flow Rule Cache (port %u): Deleted counters for rule with internal ID %" PRIu32, get_port_id(), int_rule_ids[i]);
        }
    }

    _rules_nb -= rules_nb;
}

/**
 * Performs a run-time consistency check with respect to the desired occupancy of the flow rule cache.
 *
 * @args target_number_of_rules: desired flow rule occupancy
 * @args int_vec: list of internal rule IDs to insert
 * @args glb_vec: list of global rule IDs to insert
 */
void
FlowRuleCache::cache_consistency_check(const int32_t &target_number_of_rules)
{
    if (target_number_of_rules < 0) {
        _errh->error("Flow Rule Cache (port %u): Cannot verify consistency with a negative target number of rules", get_port_id());
        return;
    }

    bool consistent = true;

    // Assertions alone are not as descriptive as prints :p
    if (_internal_rule_map.size() != target_number_of_rules) {
        consistent = false;
        _errh->error(
            "Flow Rule Cache (port %u): Number of rules in the flow rule cache's internal map %" PRIu32 " does not agree with target rules %" PRId32,
            get_port_id(), _internal_rule_map.size(), target_number_of_rules
        );
    }

    if (_rules_nb != target_number_of_rules) {
        consistent = false;
        _errh->error(
            "Flow Rule Cache (port %u): Number of rules in the flow rule cache %" PRIu32 " does not agree with target rules %" PRId32,
            get_port_id(), _rules_nb, target_number_of_rules
        );
    }

    if (_matched_pkts.size() != target_number_of_rules) {
        consistent = false;
        _errh->error(
            "Flow Rule Cache (port %u): Number of rules in the flow rule cache's packet counters %" PRIu32 " does not agree with target rules %" PRId32,
            get_port_id(), _matched_pkts.size(), target_number_of_rules
        );
    }

    if (_matched_bytes.size() != target_number_of_rules) {
        consistent = false;
        _errh->error(
            "Flow Rule Cache (port %u): Number of rules in the flow rule cache's byte counters %" PRIu32 " does not agree with target rules %" PRId32,
            get_port_id(), _matched_bytes.size(), target_number_of_rules
        );
    }

    // In case of inconsistency, print the list of rules
    if (!consistent) {
        _errh->message("Flow Rule Cache (port %u): List of rules \n", get_port_id());
    }

    auto it = _internal_rule_map.begin();
    while (it != _internal_rule_map.end()) {
        uint32_t r_id = it.key();
        uint32_t int_r_id = it.value();

        if (!_matched_pkts.findp(int_r_id) || !_matched_bytes.findp(int_r_id)) {
            _errh->error(
                "Flow Rule Cache (port %u): Internal rule ID %" PRIu32 " not present in packet/byte counters map",
                get_port_id(), int_r_id
            );
        }

        if (!consistent) {
            _errh->message("Flow Rule Cache (port %u): Rule %" PRIu32 " - %s", get_port_id(), int_r_id, get_rule_by_global_id(r_id).c_str());
        }

        it++;
    }

    // Now that we know the state of the cache, assert!
    assert(_internal_rule_map.size() == target_number_of_rules);
    assert(_rules_nb == target_number_of_rules);
    assert(_matched_pkts.size() == target_number_of_rules);
    assert(_matched_bytes.size() == target_number_of_rules);
    assert(consistent);
}

/**
 * Verifies that the list of updated internal and global IDs result in a consistent flow rule cache.
 *
 * @arg int_vec: list of internal rule IDs to verify
 * @arg glb_vec: list of global rule IDs to verify
 * @return boolean verification status
 */
bool
FlowRuleCache::verify_transactions(const Vector<uint32_t> &int_vec, const Vector<uint32_t> &glb_vec)
{
    _errh->message("====================================================================================================");
    bool consistent = true;
    for (uint32_t i : int_vec) {
        if (!internal_rule_id_exists(i)) {
            _errh->error("Flow Rule Cache (port %u): Newly inserted internal rule ID %" PRIu32 " is not present in the flow rule cache", get_port_id(), i);
            consistent = false;
        }
    }

    for (uint32_t g : glb_vec) {
        if (!global_rule_id_exists(g)) {
            _errh->error("Flow Rule Cache (port %u): Newly inserted global rule ID %" PRIu32 " is not present in the flow rule cache", get_port_id(), g);
            consistent = false;
        }
    }
    _errh->message("%s", consistent ? "Consistent flow rule cache!" : "Inconsistent rule IDs in the flow rule cache!");
    _errh->message("====================================================================================================");

    return consistent;
}

/**
 * Correlates a given candidate max internal rule ID with the flow rule cache
 * and updates it (if needed).
 * For example:
 *             Cache: 4  5  6  9  21 24 25 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 47 50 56 60 63 64 65 66
 *      Sorted Cache: 66 65 64 63 60 56 50 47 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 25 24 21  9  6  5  4
 *            Delete: 66 65 64 63 50 47
 * Candidate max IDs: 66 65 64 63
 *  Normal candidate: 63 (the least of the candidate max IDs)
 *    Real candidate: 61 (60 is the new maximum in the cache after the deletion, therefore the next ID is 60+1)
 *
 *             Cache: 4  5  6  9  21 24 25 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 47 50 56 60 63 64 65 66
 *      Sorted Cache: 66 65 64 63 60 56 50 47 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 25 24 21  9  6  5  4
 *            Delete: 66 64 63 62 50 47
 * Candidate max IDs: 66
 *  Normal candidate: 66 (the least of the candidate max IDs)
 *    Real candidate: 61 (60 is the new maximum in the cache after the deletion, therefore the next ID is 60+1)
 *
 * @arg candidate: candidate internal rule ID
 */
void
FlowRuleCache::correlate_candidate_id_with_cache(int32_t &candidate)
{
    assert(candidate >= 0);

    Vector<uint32_t> int_rule_ids = internal_rule_ids();

    sort_rule_ids_dec(int_rule_ids);

    if (_verbose) {
        _errh->message("Correlating with flow rule cache - Candidate next internal rule ID is %" PRId32, candidate);
    }

    uint32_t index = 0;
    for (uint32_t i : int_rule_ids) {
        if (i == candidate) {
            break;
        }
        index++;
    }

    // No need to update the candidate
    if (index == 0) {
        assert(int_rule_ids[index] == candidate);
        if (_verbose) {
            _errh->message("No need to update the next internal rule ID");
        }
        return;
    }
    if ((index + 1) == int_rule_ids.size()) {
        if (_verbose) {
            _errh->message("No need to update the next internal rule ID");
        }
        return;
    }

    short diff = int_rule_ids[index] - int_rule_ids[index + 1];
    if (diff > 1) {
        candidate = int_rule_ids[index + 1] + 1;
    }

    if (_verbose) {
        _errh->message("Updated next internal rule ID: %" PRId32, candidate);
    }
}

/**
 * Resets the flow rule counters of this flow rule cache.
 */
void
FlowRuleCache::flush_rule_counters()
{
    _rules_nb = 0;
    _next_rule_id = 0;
    _matched_pkts.clear();
    _matched_bytes.clear();
}

#endif /* RTE_VERSION >= RTE_VERSION_NUM(20,2,0,0) */

CLICK_ENDDECLS
