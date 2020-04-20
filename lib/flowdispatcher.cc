// -*- c-basic-offset: 4; related-file-name: "flowdispatcher.hh" -*-
/*
 * flowdispatcher.cc -- library for integrating DPDK's Flow API in Click
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
#include <click/flowdispatcher.hh>

CLICK_DECLS

#if RTE_VERSION >= RTE_VERSION_NUM(20,2,0,0)

#include <rte_flow.h>

/**
 * Flow Cache implementation.
 */

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
 * Checks whether this flow cache contains any flow rules or not.
 *
 * @return true if at least one flow rule is in the cache, otherwise false
 */
bool
FlowCache::has_rules()
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
FlowCache::global_rule_id_exists(const uint32_t &rule_id)
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
FlowCache::internal_rule_id_exists(const uint32_t &int_rule_id)
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
FlowCache::global_from_internal_rule_id(const uint32_t &int_rule_id)
{
    if (int_rule_id < 0) {
        return (uint32_t) _errh->error("Flow Cache (port %u): Unable to verify mapping due to invalid internal NIC rule ID %" PRIu32, get_port_id(), int_rule_id);
    }

    auto it = _internal_rule_map.begin();
    while (it != _internal_rule_map.end()) {
        uint32_t r_id = it.key();
        uint32_t int_r_id = it.value();

        if (int_r_id == int_rule_id) {
            // if (_verbose) {
            //     _errh->message("Flow Cache (port %u): Internal rule ID %" PRIu32 " is mapped to global rule ID %ld", get_port_id(), int_rule_id, r_id);
            // }
            return r_id;
        }

        it++;
    }

    if (_verbose) {
        _errh->message("Flow Cache (port %u): Internal rule ID %" PRIu32 " does not exist in the flow cache", get_port_id(), int_rule_id);
    }

    return (uint32_t) FLOWDISP_ERROR;
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
FlowCache::internal_from_global_rule_id(const uint32_t &rule_id)
{
    if (rule_id < 0) {
        return (int32_t) _errh->error("Flow Cache (port %u): Unable to verify mapping due to invalid global NIC rule ID %" PRIu32, get_port_id(), rule_id);
    }

    const uint32_t *found = _internal_rule_map.findp(rule_id);
    if (!found) {
        if (_verbose) {
            _errh->message("Flow Cache (port %u): Global rule ID %" PRIu32 " does not exist in the flow cache", get_port_id(), rule_id);
        }
        return (int32_t) FLOWDISP_ERROR;
    }

    // if (_verbose) {
    //     _errh->message("Flow Cache (port %u): Global rule ID %" PRIu32 " is mapped to internal rule ID %" PRIu32, get_port_id(), rule_id, *found);
    // }

    return (int32_t) *found;
}

/**
 * Sorts a list of flow rules IDs by increasing order.
 *
 * @arg rule_ids_vec: a list of rule IDs to sort
 */
template<typename T>
void
FlowCache::sort_rule_ids_inc(Vector<T> &rule_ids_vec)
{
    for (uint32_t i = 1; i < rule_ids_vec.size(); ++i) {
        for (uint32_t j = 0; j < rule_ids_vec.size(); ++j) {
            if (rule_ids_vec[j] > rule_ids_vec[i]) {
                T temp = rule_ids_vec[j];
                rule_ids_vec[j] = rule_ids_vec[i];
                rule_ids_vec[i] = temp;
            }
        }
    }
}

/**
 * Sorts a a list of flow rules IDs by decreasing order.
 *
 * @arg rule_ids_vec: a list of rule IDs to sort
 */
template<typename T>
void
FlowCache::sort_rule_ids_dec(Vector<T> &rule_ids_vec)
{
    for (uint32_t i = 0; i < rule_ids_vec.size(); ++i) {
        for (uint32_t j = i; j < rule_ids_vec.size(); ++j) {
            if (rule_ids_vec[j] > rule_ids_vec[i]) {
                T temp = rule_ids_vec[j];
                rule_ids_vec[j] = rule_ids_vec[i];
                rule_ids_vec[i] = temp;
            }
        }
    }
}

/**
 * Returns the list of global flow rules IDs in this flow cache.
 *
 * @arg increasing: boolean flag that indicates the order of rule IDs (defaults to true)
 * @return list of global flow rules IDs
 */
Vector<uint32_t>
FlowCache::global_rule_ids(const bool increasing)
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
 * Returns the list of internal flow rules IDs in this flow cache.
 *
 * @arg increasing: boolean flag that indicates the order of rule IDs (defaults to true)
 * @return list of internal flow rules IDs
 */
Vector<uint32_t>
FlowCache::internal_rule_ids(const bool increasing)
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
FlowCache::internal_rule_ids_counters(const bool increasing)
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
FlowCache::rules_map_by_core_id(const int &core_id)
{
    if (core_id < 0) {
        _errh->error("Flow Cache (port %u): Unable to find rule map due to invalid CPU core ID %d", get_port_id(), core_id);
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
FlowCache::rules_list_by_core_id(const int &core_id)
{
    Vector<String> rules_vec;

    if (core_id < 0) {
        _errh->error("Flow Cache (port %u): Unable to find rules due to invalid CPU core ID %d", get_port_id(), core_id);
        return rules_vec;
    }

    HashMap<uint32_t, String> *rules_map = rules_map_by_core_id(core_id);
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
 * Returns the number of CPU cores that have at least one flow rule each.
 *
 * @return a list CPU cores that have associated flow rules
 */
Vector<int>
FlowCache::cores_with_rules()
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
FlowCache::get_rule_by_global_id(const uint32_t &rule_id)
{
    if (rule_id < 0) {
        _errh->error("Flow Cache (port %u): Unable to print rule with invalid rule ID %" PRIu32, get_port_id(), rule_id);
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
FlowCache::get_rule_by_internal_id(const uint32_t &int_rule_id)
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
FlowCache::store_rule_id_mapping(const uint32_t &rule_id, const uint32_t &int_rule_id)
{
    if (rule_id < 0) {
        _errh->error("Flow Cache (port %u): Unable to store mapping due to invalid rule ID %" PRIu32, get_port_id(), rule_id);
        return false;
    }

    if (!_internal_rule_map.insert(rule_id, int_rule_id)) {
        _errh->error("Flow Cache (port %u): Failed to inserted rule mapping %" PRIu32 " <--> %" PRIu32, get_port_id(), rule_id, int_rule_id);
        return false;
    }

    if (_verbose) {
        _errh->message("Flow Cache (port %u): Successfully inserted rule mapping %" PRIu32 " <--> %" PRIu32, get_port_id(), rule_id, int_rule_id);
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
FlowCache::delete_rule_id_mapping(const uint32_t &rule_id)
{
    if (rule_id < 0) {
        _errh->error("Flow Cache (port %u): Unable to delete mapping for invalid rule ID %" PRIu32, get_port_id(), rule_id);
        return false;
    }

    if (_internal_rule_map.remove(rule_id)) {
        if (_verbose) {
            _errh->message("Flow Cache (port %u): Successfully deleted mapping for rule ID %" PRIu32, get_port_id(), rule_id);
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
FlowCache::currently_max_internal_rule_id()
{
    return _next_rule_id - 1;
}

/**
 * Returns the next internal flow rule ID to use for insertion.
 *
 * @return next available internal flow rule ID
 */
uint32_t
FlowCache::next_internal_rule_id()
{
    return _next_rule_id++;
}

/**
 * Sets the next internal flow rule ID to use for insertion.
 *
 * @arg next_id: next available internal flow rule ID
 */
void
FlowCache::set_next_internal_rule_id(uint32_t next_id)
{
    _next_rule_id = next_id;
}

/**
 * Adds a new flow rule to this flow cache.
 *
 * @args core_id: a CPU core ID associated with the flow rule
 * @args rule_id: a global rule ID associated with the flow rule
 * @args int_rule_id: an internal flow rule ID associated with the flow rule
 * @args rule: the actual flow rule
 * @return 0 upon success, otherwise a negative integer
 */
int
FlowCache::insert_rule_in_flow_cache(const int &core_id, const uint32_t &rule_id, const uint32_t &int_rule_id, const String rule)
{
    if (core_id < 0) {
        return _errh->error("Flow Cache (port %u): Unable to add rule due to invalid CPU core ID %d", get_port_id(), core_id);
    }

    if (rule_id < 0) {
        return _errh->error("Flow Cache (port %u): Unable to add rule due to invalid rule ID %" PRIu32, get_port_id(), rule_id);
    }

    if (int_rule_id < 0) {
        return _errh->error("Flow Cache (port %u): Unable to add rule due to invalid internal rule ID %" PRIu32, get_port_id(), int_rule_id);
    }

    if (rule.empty()) {
        return _errh->error("Flow Cache (port %u): Unable to add rule due to empty input", get_port_id());
    }

    HashMap<uint32_t, String> *rules_map = rules_map_by_core_id(core_id);
    if (!rules_map) {
        _rules.insert(core_id, new HashMap<uint32_t, String>());
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
            "Flow Cache (port %u): Rule %" PRIu32 " added and mapped with internal rule ID %" PRIu32 " and queue %d",
            get_port_id(), rule_id, int_rule_id, core_id
        );
    }

    return FLOWDISP_SUCCESS;
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
FlowCache::update_rule_in_flow_cache(const int &core_id, const uint32_t &rule_id, const uint32_t &int_rule_id, String rule)
{
    // First try to delete this rule, if it exists
    delete_rule_by_global_id(rule_id);

    // Now, store this new rule in this CPU core's flow cache
    return (insert_rule_in_flow_cache(core_id, rule_id, int_rule_id, rule) == FLOWDISP_SUCCESS);
}

/**
 * Deletes a flow rule from this flow cache using its global ID as an index.
 *
 * @args rule_id: the global flow rule ID of the flow rule to be deleted
 * @return the internal flow rule ID being deleted upon success, otherwise a negative integer
 */
int32_t
FlowCache::delete_rule_by_global_id(const uint32_t &rule_id)
{
    if (rule_id < 0) {
        return _errh->error("Flow Cache (port %u): Unable to delete rule due to invalid global rule ID %" PRIu32, get_port_id(), rule_id);
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
                return _errh->error("Flow Cache (port %u): Unable to delete rule %" PRIu32 " due to no internal mapping", get_port_id(), rule_id);
            }

            // Now delete this mapping
            if (!delete_rule_id_mapping(rule_id)) {
                return FLOWDISP_ERROR;
            }

            if (_verbose) {
                _errh->message(
                    "Flow Cache (port %u): Rule with global ID %" PRIu32 " and internal rule ID %" PRIu32 " deleted from queue %d",
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
        _errh->message("Flow Cache (port %u): Unable to delete rule %" PRIu32 " due to cache miss", get_port_id(), rule_id);
    }

    return FLOWDISP_ERROR;
}

/**
 * Deletes a list of flow ules from this flow cache.
 *
 * @args int_rule_ids: an array of internal flow rule IDs to be deleted
 * @args rules_nb: the number of flow rule IDs to be deleted
 * @return a space-separated list of deleted internal flow rule IDs upon success, otherwise an empty string
 */
String
FlowCache::delete_rules_by_internal_id(const uint32_t *int_rule_ids, const uint32_t &rules_nb)
{
    String int_rule_ids_str = "";
    for (uint32_t i = 0; i < rules_nb; i++) {
        int_rule_ids_str += String(int_rule_ids[i]) + " ";
    }

    return delete_rules_by_internal_id(int_rule_ids_str.trim_space().split(' '));
}

/**
 * Deletes a list of flow rules from this flow cache.
 *
 * @args rules_vec: a vector of flow rule IDs to be deleted
 * @return a space-separated list of deleted internal flow rule IDs upon success, otherwise an empty string
 */
String
FlowCache::delete_rules_by_internal_id(const Vector<String> &rules_vec)
{
    if (rules_vec.empty()) {
        _errh->error("Flow Cache (port %u): No flow rule mappings to delete", get_port_id());
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
            _errh->error("Flow Cache (port %u): Unable to delete mapping for rule with internal ID %" PRIu32, get_port_id(), int_rule_id);
            return rule_ids_str.trim_space();
        }

        if (delete_rule_by_global_id(rule_id) < 0) {
            _errh->error("Flow Cache (port %u): Unable to delete mapping for rule with global ID %" PRIu32, get_port_id(), rule_id);
            return rule_ids_str.trim_space();
        }

        rule_ids_str += String(int_rule_id) + " ";

        deleted_rules++;
        it++;
    }

    if (_verbose) {
        _errh->message("Flow Cache (port %u): Deleted mappings for %" PRIu32 "/%" PRIu32 " rules", get_port_id(), deleted_rules, rules_to_delete);
    }

    return rule_ids_str.trim_space();
}

/**
 * Deletes all flow rules from this flow cache.
 *
 * @return the number of flushed flow rules upon success, otherwise a negative integer
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
        _errh->message("Flow Cache (port %u): Successfully deleted %" PRId32 " rules from flow cache", get_port_id(), flushed_rules_nb);
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
FlowCache::set_matched_packets(const uint32_t &int_rule_id, uint64_t value)
{
    if (int_rule_id < 0) {
        _errh->error("Flow Cache (port %u): Cannot update packet counters of invalid rule ID %" PRIu32, get_port_id(), int_rule_id);
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
FlowCache::get_matched_packets(const uint32_t &int_rule_id)
{
    if (int_rule_id < 0) {
        _errh->error("Flow Cache (port %u): No packet counters for invalid rule ID %" PRIu32, get_port_id(), int_rule_id);
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
FlowCache::set_matched_bytes(const uint32_t &int_rule_id, uint64_t value)
{
    if (int_rule_id < 0) {
        _errh->error("Flow Cache (port %u): Cannot update byte counters of invalid rule ID %" PRIu32, get_port_id(), int_rule_id);
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
FlowCache::get_matched_bytes(const uint32_t &int_rule_id)
{
    if (int_rule_id < 0) {
        _errh->error("Flow Cache (port %u): No byte counters for invalid rule ID %" PRIu32, get_port_id(), int_rule_id);
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
FlowCache::initialize_rule_counters(uint32_t *int_rule_ids, const uint32_t &rules_nb)
{
    if (!int_rule_ids || (rules_nb <= 0)) {
        _errh->error("Flow Cache (port %u): Cannot initialize flow counters; no rule IDs provided", get_port_id());
        return;
    }

    for (uint32_t i = 0; i < rules_nb; i++) {
        _matched_pkts.insert(int_rule_ids[i], 0);
        _matched_bytes.insert(int_rule_ids[i], 0);
        if (_verbose) {
            _errh->message("Flow Cache (port %u): Initialized counters for rule with internal ID %" PRIu32, get_port_id(), int_rule_ids[i]);
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
FlowCache::delete_rule_counters(uint32_t *int_rule_ids, const uint32_t &rules_nb)
{
    if (!int_rule_ids || (rules_nb <= 0)) {
        _errh->error("Flow Cache (port %u): Cannot delete flow counters; no rule IDs provided", get_port_id());
        return;
    }

    for (uint32_t i = 0; i < rules_nb; i++) {
        _matched_pkts.erase(int_rule_ids[i]);
        _matched_bytes.erase(int_rule_ids[i]);
        if (_verbose) {
            _errh->message("Flow Cache (port %u): Deleted counters for rule with internal ID %" PRIu32, get_port_id(), int_rule_ids[i]);
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
FlowCache::cache_consistency_check(const int32_t &target_number_of_rules)
{
    if (target_number_of_rules < 0) {
        _errh->error("Flow Cache (port %u): Cannot verify consistency with a negative target number of rules", get_port_id());
        return;
    }

    bool consistent = true;

    // Assertions alone are not as descriptive as prints :p
    if (_internal_rule_map.size() != target_number_of_rules) {
        consistent = false;
        _errh->error(
            "Flow Cache (port %u): Number of rules in the flow cache's internal map %" PRIu32 " does not agree with target rules %" PRId32,
            get_port_id(), _internal_rule_map.size(), target_number_of_rules
        );
    }

    if (_rules_nb != target_number_of_rules) {
        consistent = false;
        _errh->error(
            "Flow Cache (port %u): Number of rules in the flow cache %" PRIu32 " does not agree with target rules %" PRId32,
            get_port_id(), _rules_nb, target_number_of_rules
        );
    }

    if (_matched_pkts.size() != target_number_of_rules) {
        consistent = false;
        _errh->error(
            "Flow Cache (port %u): Number of rules in the flow cache's packet counters %" PRIu32 " does not agree with target rules %" PRId32,
            get_port_id(), _matched_pkts.size(), target_number_of_rules
        );
    }

    if (_matched_bytes.size() != target_number_of_rules) {
        consistent = false;
        _errh->error(
            "Flow Cache (port %u): Number of rules in the flow cache's byte counters %" PRIu32 " does not agree with target rules %" PRId32,
            get_port_id(), _matched_bytes.size(), target_number_of_rules
        );
    }

    // In case of inconsistency, print the list of rules
    if (!consistent) {
        _errh->message("Flow Cache (port %u): List of rules \n", get_port_id());
    }

    auto it = _internal_rule_map.begin();
    while (it != _internal_rule_map.end()) {
        uint32_t r_id = it.key();
        uint32_t int_r_id = it.value();

        if (!_matched_pkts.findp(int_r_id) || !_matched_bytes.findp(int_r_id)) {
            _errh->error(
                "Flow Cache (port %u): Internal rule ID %" PRIu32 " not present in packet/byte counters map",
                get_port_id(), int_r_id
            );
        }

        if (!consistent) {
            _errh->message("Flow Cache (port %u): Rule %" PRIu32 " - %s", get_port_id(), int_r_id, get_rule_by_global_id(r_id).c_str());
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
FlowCache::verify_transactions(const Vector<uint32_t> &int_vec, const Vector<uint32_t> &glb_vec)
{
    _errh->message("====================================================================================================");
    bool consistent = true;
    for (uint32_t i : int_vec) {
        if (!internal_rule_id_exists(i)) {
            _errh->error("Flow Cache (port %u): Newly inserted internal rule ID %" PRIu32 " is not present in the flow cache", get_port_id(), i);
            consistent = false;
        }
    }

    for (uint32_t g : glb_vec) {
        if (!global_rule_id_exists(g)) {
            _errh->error("Flow Cache (port %u): Newly inserted global rule ID %" PRIu32 " is not present in the flow cache", get_port_id(), g);
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
FlowCache::correlate_candidate_id_with_cache(int32_t &candidate)
{
    assert(candidate >= 0);

    Vector<uint32_t> int_rule_ids = internal_rule_ids();

    sort_rule_ids_dec(int_rule_ids);

    if (_verbose) {
        _errh->message("Correlating with flow cache - Candidate next internal rule ID is %" PRId32, candidate);
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
 * Flow Dispatcher implementation.
 */
// DPDKDevice mode is Flow Dispatcher
String FlowDispatcher::DISPATCHING_MODE = "flow";

// Supported Flow Dispatcher handlers (called from FromDPDKDevice)
String FlowDispatcher::FLOW_RULE_ADD             = "rule_add";
String FlowDispatcher::FLOW_RULE_DEL             = "rules_del";
String FlowDispatcher::FLOW_RULE_IDS_GLB         = "rules_ids_global";
String FlowDispatcher::FLOW_RULE_IDS_INT         = "rules_ids_internal";
String FlowDispatcher::FLOW_RULE_PACKET_HITS     = "rule_packet_hits";
String FlowDispatcher::FLOW_RULE_BYTE_COUNT      = "rule_byte_count";
String FlowDispatcher::FLOW_RULE_AGGR_STATS      = "rules_aggr_stats";
String FlowDispatcher::FLOW_RULE_LIST            = "rules_list";
String FlowDispatcher::FLOW_RULE_LIST_WITH_HITS  = "rules_list_with_hits";
String FlowDispatcher::FLOW_RULE_COUNT           = "rules_count";
String FlowDispatcher::FLOW_RULE_COUNT_WITH_HITS = "rules_count_with_hits";
String FlowDispatcher::FLOW_RULE_ISOLATE         = "rules_isolate";
String FlowDispatcher::FLOW_RULE_FLUSH           = "rules_flush";

// Set of flow rule items supported by the Flow API
HashMap<int, String> FlowDispatcher::flow_item;

// Set of flow rule actions supported by the Flow API
HashMap<int, String> FlowDispatcher::flow_action;

// Default verbosity settings
bool FlowDispatcher::DEF_VERBOSITY = false;
bool FlowDispatcher::DEF_DEBUG_MODE = false;

// Global table of DPDK ports mapped to their Flow Dispatcher objects
HashTable<portid_t, FlowDispatcher *> FlowDispatcher::dev_flow_disp;

// Map of ports to their flow rule installation/deletion statistics
HashMap<portid_t, Vector<RuleTiming>> FlowDispatcher::_rule_inst_stats_map;
HashMap<portid_t, Vector<RuleTiming>> FlowDispatcher::_rule_del_stats_map;

// Isolation mode per port
HashMap<portid_t, bool> FlowDispatcher::_isolated;

// A unique parser
struct cmdline *FlowDispatcher::_parser = NULL;

FlowDispatcher::FlowDispatcher() :
        _port_id(-1), _active(false), _verbose(DEF_VERBOSITY), _debug_mode(DEF_DEBUG_MODE), _rules_filename("")
{
    _errh = new ErrorVeneer(ErrorHandler::default_handler());
    _flow_cache = 0;
}

FlowDispatcher::FlowDispatcher(portid_t port_id, ErrorHandler *errh) :
        _port_id(port_id), _active(false), _verbose(DEF_VERBOSITY), _debug_mode(DEF_DEBUG_MODE), _rules_filename("")
{
    _errh = new ErrorVeneer(errh);
    _flow_cache = new FlowCache(port_id, _verbose, _debug_mode, _errh);

    populate_supported_flow_items_and_actions();

    if (verbose()) {
        _errh->message("Flow Dispatcher (port %u): Created (state %s)", _port_id, _active ? "active" : "inactive");
    }
}

FlowDispatcher::~FlowDispatcher()
{
    // Destroy the parser
    if (_parser) {
        cmdline_quit(_parser);
        delete _parser;
        _parser = NULL;
        if (verbose()) {
            _errh->message("Flow Dispatcher (port %u): Parser deleted", _port_id);
        }
    }

    if (_isolated.size() > 0) {
        _isolated.clear();
    }

    if (_flow_cache) {
        delete _flow_cache;
    }

    flow_item.clear();
    flow_action.clear();

    if (verbose()) {
        _errh->message("Flow Dispatcher (port %u): Destroyed", _port_id);
    }

    if (_rule_inst_stats_map.size() > 0) {
        _rule_inst_stats_map.clear();
    }

    if (_rule_del_stats_map.size() > 0) {
        _rule_del_stats_map.clear();
    }

    delete_error_handler();
}

void
FlowDispatcher::populate_supported_flow_items_and_actions()
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
 * Obtains an instance of the Flow Dispatcher parser.
 *
 * @args errh: an instance of the error handler
 * @return a Flow Dispatcher parser object
 */
struct cmdline *
FlowDispatcher::parser(ErrorHandler *errh)
{
    if (!_parser) {
        return flow_parser_init(errh);
    }

    return _parser;
}

/**
 * Obtains the flow cache associated with this Flow Dispatcher.
 *
 * @return a Flow Cache object
 */
FlowCache *
FlowDispatcher::get_flow_cache()
{
    return _flow_cache;
}

/**
 * Returns the global map of DPDK ports to
 * their Flow Dispatcher instances.
 *
 * @return a Flow Dispatcher instance map
 */
HashTable<portid_t, FlowDispatcher *>
FlowDispatcher::flow_dispatcher_map()
{
    return dev_flow_disp;
}

/**
 * Cleans the global map of DPDK ports to
 * their Flow Dispatcher instances.
 */
void
FlowDispatcher::clean_flow_dispatcher_map()
{
    if (!dev_flow_disp.empty()) {
        dev_flow_disp.clear();
    }
}

/**
 * Manages the Flow Dispatcher instances.
 *
 * @args port_id: the ID of the NIC
 * @args errh: an instance of the error handler
 * @return a Flow Dispatcher object for this NIC
 */
FlowDispatcher *
FlowDispatcher::get_flow_dispatcher(const portid_t &port_id, ErrorHandler *errh)
{
    if (!errh) {
        errh = ErrorHandler::default_handler();
    }

    // Invalid port ID
    if (port_id >= DPDKDevice::dev_count()) {
        errh->error("Flow Dispatcher (port %u): Denied to create instance for invalid port", port_id);
        return NULL;
    }

    // Get the Flow Dispatcher of the desired port
    FlowDispatcher *flow_disp = dev_flow_disp.get(port_id);

    // Not there, let's created it
    if (!flow_disp) {
        flow_disp = new FlowDispatcher(port_id, errh);
        assert(flow_disp);
        dev_flow_disp[port_id] = flow_disp;
    }

    // Create a Flow Dispatcher parser
    _parser = parser(errh);

    // Ship it back
    return flow_disp;
}

/**
 * Calibrates the flow rule cache before new rule(s) are inserted.
 * Transforms the input array into a map and calls an overloaded calibrate_cache.
 *
 * @arg int_rule_ids: an array of internal flow rule IDs to be deleted
 * @arg rules_nb: the number of flow rules to be deleted
 */
void
FlowDispatcher::calibrate_cache(const uint32_t *int_rule_ids, const uint32_t &rules_nb)
{
    HashMap<uint32_t, String> rules_map;
    for (uint32_t i = 0; i < rules_nb ; i++) {
        uint32_t rule_id = _flow_cache->global_from_internal_rule_id(int_rule_ids[i]);
        // We only need the rule IDs, not the actual rules
        rules_map.insert(rule_id, "");
    }

    calibrate_cache(rules_map);
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
FlowDispatcher::calibrate_cache(const HashMap<uint32_t, String> &rules_map)
{
    bool calibrate = false;
    Vector<uint32_t> candidates;
    int32_t max_int_id = _flow_cache->currently_max_internal_rule_id();

    // Now insert each rule in the flow cache
    auto it = rules_map.begin();
    while (it != rules_map.end()) {
        uint32_t rule_id = it.key();
        int32_t int_id = _flow_cache->internal_from_global_rule_id(rule_id);
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
    _flow_cache->sort_rule_ids_dec(candidates);

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
    _flow_cache->correlate_candidate_id_with_cache(the_candidate);

    // Update the next internal rule ID
    _flow_cache->set_next_internal_rule_id(the_candidate);
}

/**
 * Returns a set of string-based flow rules read from a file.
 *
 * @args filename: the file that contains the flow rules
 * @return a string of newline-separated flow rules in memory
 */
String
FlowDispatcher::load_rules_from_file_to_string(const String &filename)
{
    String rules_str = "";

    if (filename.empty()) {
        _errh->warning("Flow Dispatcher (port %u): No file provided", _port_id);
        return rules_str;
    }

    FILE *fp = NULL;
    fp = fopen(filename.c_str(), "r");
    if (fp == NULL) {
        _errh->error("Flow Dispatcher (port %u): Failed to open file '%s'", _port_id, filename.c_str());
        return rules_str;
    }
    _errh->message("Flow Dispatcher (port %u): Opened file '%s'", _port_id, filename.c_str());

    uint32_t rules_nb = 0;
    uint32_t loaded_rules_nb = 0;

    char *line = NULL;
    size_t len = 0;
    const char ignore_chars[] = "\n\t ";

    // Read file line-by-line (or rule-by-rule)
    while ((getline(&line, &len, fp)) != -1) {
        rules_nb++;

        // Skip empty lines or lines with only spaces/tabs
        if (!line || (strlen(line) == 0) ||
            (strchr(ignore_chars, line[0]))) {
            _errh->warning("Flow Dispatcher (port %u): Invalid rule #%" PRIu32, _port_id, rules_nb);
            continue;
        }

        // Detect and remove unwanted components
        String rule = String(line);
        if (!filter_rule(rule)) {
            _errh->error("Flow Dispatcher (port %u): Invalid rule '%s'", _port_id, line);
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

    _errh->message("Flow Dispatcher (port %u): Loaded %" PRIu32 "/%" PRIu32 " rules", _port_id, loaded_rules_nb, rules_nb);

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
FlowDispatcher::update_rules(const HashMap<uint32_t, String> &rules_map, bool by_controller, int core_id)
{
    uint32_t rules_to_install = rules_map.size();
    if (rules_to_install == 0) {
        return (int32_t) _errh->error("Flow Dispatcher (port %u): Failed to add rules due to empty input map", _port_id);
    }

    // Current capacity
    int32_t capacity = (int32_t) flow_rules_count();

    // Prepare the cache counter for new deletions and insertions
    calibrate_cache(rules_map);

    String rules_str = "";
    uint32_t installed_rules_nb = 0;

    // Initialize the counters for the new internal rule ID
    uint32_t *int_rule_ids = (uint32_t *) malloc(rules_to_install * sizeof(uint32_t));
    if (!int_rule_ids) {
        return (int32_t) _errh->error("Flow Dispatcher (port %u): Failed to allocate space to store %" PRIu32 " rule IDs", _port_id, rules_to_install);
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
        int32_t old_int_rule_id = _flow_cache->internal_from_global_rule_id(rule_id);

        // Get the right internal rule ID
        uint32_t int_rule_id = 0;
        if (by_controller) {
            int_rule_id = _flow_cache->next_internal_rule_id();
        } else {
            int_rule_id = rule_id;
        }

        if (_verbose) {
            _errh->message(
                "Flow Dispatcher (port %u): About to install rule with global ID %" PRIu32 " and internal ID %" PRIu32 " on core %d: %s",
                _port_id, rule_id, int_rule_id, core_id, rule.c_str()
            );
        }

        // Update the flow cache
        if (!_flow_cache->update_rule_in_flow_cache(core_id, rule_id, int_rule_id, rule)) {
            return FLOWDISP_ERROR;
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
    _flow_cache->initialize_rule_counters(int_rule_ids, installed_rules_nb);
    // Now delete the buffer to avoid memory leaks
    free(int_rule_ids);

    uint32_t old_rules_to_delete = old_int_rule_ids_vec.size();

    // First delete existing rules (if any)
    if (flow_rules_delete(old_int_rule_ids_vec, false) != old_rules_to_delete) {
        return FLOWDISP_ERROR;
    }

    if (_debug_mode) {
        // Verify that what we deleted is not in the flow cache anynore
        assert(flow_rules_verify_absence(old_int_rule_ids_vec) == FLOWDISP_SUCCESS);
    }

    RuleTiming rits(_port_id);
    rits.start = Timestamp::now_steady();

    // Install in the NIC as a batch
    if (flow_rules_install(rules_str, installed_rules_nb) != FLOWDISP_SUCCESS) {
        return FLOWDISP_ERROR;
    }

    rits.end = Timestamp::now_steady();

    rits.update(installed_rules_nb);
    add_rule_inst_stats(rits);

    if (_debug_mode) {
        // Verify that what we inserted is in the flow cache
        assert(flow_rules_verify_presence(int_rule_ids_vec) == FLOWDISP_SUCCESS);
    }

    // Debugging stuff
    if (_debug_mode || _verbose) {
        capacity = (capacity == 0) ? (int32_t) flow_rules_count() : capacity;
        rule_consistency_check(capacity);
    }

    _errh->message(
        "Flow Dispatcher (port %u): Successfully installed %" PRIu32 "/%" PRIu32 " rules in %.2f ms at the rate of %.3f rules/sec",
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
FlowDispatcher::add_rules_from_file(const String &filename)
{
    HashMap<uint32_t, String> rules_map;
    const String rules_str = (const String) load_rules_from_file_to_string(filename);

    if (rules_str.empty()) {
        return (int32_t) _errh->error("Flow Dispatcher (port %u): Failed to add rules due to empty input from file", _port_id);
    }

    // Tokenize them to facilitate the insertion in the flow cache
    Vector<String> rules_vec = rules_str.trim_space().split('\n');

    for (uint32_t i = 0; i < rules_vec.size(); i++) {
        String rule = rules_vec[i] + "\n";

        // Obtain the right internal rule ID
        uint32_t next_int_rule_id = _flow_cache->next_internal_rule_id();

        // Add rule to the map
        rules_map.insert((uint32_t) next_int_rule_id, rule);
    }

    return update_rules(rules_map, false);
}

/**
 * Translates a set of newline-separated flow rules into flow rule objects and installs them in a NIC.
 *
 * @args rules: a string of newline-separated flow rules
 * @args rules_nb: the number of flow rules to install
 * @return installation status
 */
int
FlowDispatcher::flow_rules_install(const String &rules, const uint32_t &rules_nb)
{
    // Only active instances can configure a NIC
    if (!active()) {
        _errh->error("Flow Dispatcher (port %u): Inactive instance cannot install rules", _port_id);
        return FLOWDISP_ERROR;
    }

    uint32_t rules_before = flow_rules_count_explicit();

    // TODO: Fix DPDK to return proper status
    int res = flow_parser_parse(_parser, (const char *) rules.c_str(), _errh);

    uint32_t rules_after = flow_rules_count_explicit();

    if (res >= 0) {
        // Workaround DPDK's deficiency to report rule installation issues
        if ((rules_before + rules_nb) != rules_after) {
            _errh->message("Flow Dispatcher (port %u): Flow installation failed - Has %" PRIu32 ", but expected %" PRIu32 " rules", _port_id, rules_after, rules_before + rules_nb);
            return FLOWDISP_ERROR;
        } else {
            _errh->message("Flow Dispatcher (port %u): Parsed and installed a batch of %" PRIu32 " rules", _port_id, rules_nb);
            return FLOWDISP_SUCCESS;
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
        _errh->error("Flow Dispatcher (port %u): Partially installed %" PRIu32 "/%" PRIu32 " rules", _port_id, (rules_after - rules_before), rules_nb);
    }
    _errh->error("Flow Dispatcher (port %u): Failed to parse rules due to %s", _port_id, error.c_str());

    return FLOWDISP_ERROR;
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
 * @return installation status
 */
int
FlowDispatcher::flow_rule_install(const uint32_t &int_rule_id, const uint32_t &rule_id, const int &core_id, const String &rule, const bool with_cache)
{
    // Insert in NIC
    if (flow_rules_install(rule, 1) != FLOWDISP_SUCCESS) {
        return FLOWDISP_ERROR;
    }

    // Update flow cache, If asked to do so
    if (with_cache) {
        int32_t old_int_rule_id = _flow_cache->internal_from_global_rule_id(rule_id);
        if (!_flow_cache->update_rule_in_flow_cache(core_id, rule_id, int_rule_id, rule)) {
            return FLOWDISP_ERROR;
        } else {
            uint32_t int_rule_ids[1] = {(uint32_t) int_rule_id};
            _flow_cache->initialize_rule_counters(int_rule_ids, 1);
        }
        if (old_int_rule_id >= 0) {
            uint32_t old_int_rule_ids[1] = {(uint32_t) old_int_rule_id};
            return (flow_rules_delete(old_int_rule_ids, 1) == 1);
        }
        return FLOWDISP_SUCCESS;
    }

    return FLOWDISP_SUCCESS;
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
FlowDispatcher::flow_rules_verify(const Vector<uint32_t> &int_rule_ids_vec, const Vector<uint32_t> &old_int_rule_ids_vec)
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
FlowDispatcher::flow_rules_verify_presence(const Vector<uint32_t> &int_rule_ids_vec)
{
    bool verified = true;

    _errh->message("====================================================================================================");

    for (auto int_id : int_rule_ids_vec) {
        if (!flow_rule_get(int_id)) {
            verified = false;
            String message = "Flow Dispatcher (port " + String(_port_id) + "): Rule " + String(int_id) + " is not in the NIC";
            if (_verbose) {
                String rule = _flow_cache->get_rule_by_internal_id(int_id);
                assert(!rule.empty());
                message += " " + rule;
            }
            _errh->error("%s", message.c_str());
        }
    }

    _errh->message("Presence of internal rule IDs: %s", verified ? "Verified" : "Not verified");
    _errh->message("====================================================================================================");

    return verified ? FLOWDISP_SUCCESS : FLOWDISP_ERROR;
}

/**
 * Verifies that a list of old flow rule IDs is absent from the NIC.
 *
 * @args old_int_rule_ids_vec: a list of old flow rules to verify their absense
 * @return absence status
 */
int
FlowDispatcher::flow_rules_verify_absence(const Vector<uint32_t> &old_int_rule_ids_vec)
{
    bool verified = true;

    _errh->message("====================================================================================================");

    for (auto int_id : old_int_rule_ids_vec) {
        if (flow_rule_get(int_id)) {
            verified = false;
            String message = "Flow Dispatcher (port " + String(_port_id) + "): Rule " + String(int_id) + " is still in the NIC";
            if (_verbose) {
                String rule = _flow_cache->get_rule_by_internal_id(int_id);
                assert(!rule.empty());
                message += " " + rule;
            }
            _errh->error("%s", message.c_str());
        }
    }

    _errh->message("Absence of internal rule IDs: %s", verified ? "Verified" : "Not verified");
    _errh->message("====================================================================================================");

    return verified ? FLOWDISP_SUCCESS : FLOWDISP_ERROR;
}

/**
 * Returns a flow rule object of a specific NIC with specific internal flow rule ID.
 *
 * @args int_rule_id: an internal flow rule ID
 * @return a flow rule object
 */
struct port_flow *
FlowDispatcher::flow_rule_get(const uint32_t &int_rule_id)
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
FlowDispatcher::flow_rules_delete(const Vector<uint32_t> &old_int_rule_ids_vec, const bool with_cache)
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
FlowDispatcher::flow_rules_delete(uint32_t *int_rule_ids, const uint32_t &rules_nb, const bool with_cache)
{
    // Only active instances can configure a NIC
    if (!active()) {
        return _errh->error("Flow Dispatcher (port %u): Inactive instance cannot remove rules", _port_id);
    }

    // Inputs' sanity check
    if ((!int_rule_ids) || (rules_nb == 0)) {
        return _errh->error("Flow Dispatcher (port %u): No rules to remove", _port_id);
    }

    RuleTiming rdts(_port_id);
    rdts.start = Timestamp::now_steady();

    // TODO: For N rules, port_flow_destroy calls rte_flow_destroy N times.
    // TODO: If one of the rule IDs in this array is invalid, port_flow_destroy still succeeds.
    //       DPDK must act upon these issues.
    if (port_flow_destroy(_port_id, (uint32_t) rules_nb, (const uint32_t *) int_rule_ids) != FLOWDISP_SUCCESS) {
        return _errh->error(
            "Flow Dispatcher (port %u): Failed to remove a batch of %" PRIu32 " rules",
            _port_id, rules_nb
        );
    }

    rdts.end = Timestamp::now_steady();

    rdts.update((uint32_t) rules_nb);
    add_rule_del_stats(rdts);

    // Update flow cache
    if (with_cache) {
        // First calibrate the cache
        calibrate_cache(int_rule_ids, rules_nb);

        String rule_ids_str = _flow_cache->delete_rules_by_internal_id(int_rule_ids, rules_nb);
        if (rule_ids_str.empty()) {
            return FLOWDISP_ERROR;
        }
    }

    _errh->message(
        "Flow Dispatcher (port %u): Successfully deleted %" PRIu32 " rules in %.2f ms at the rate of %.3f rules/sec",
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
FlowDispatcher::flow_rules_isolate(const portid_t &port_id, const int &set)
{
#if RTE_VERSION >= RTE_VERSION_NUM(17,8,0,0)
    if (port_flow_isolate(port_id, set) != FLOWDISP_SUCCESS) {
        ErrorHandler *errh = ErrorHandler::default_handler();
        return errh->error(
            "Flow Dispatcher (port %u): Failed to restrict ingress traffic to the defined flow rules", port_id
        );
    }

    return 0;
#else
    ErrorHandler *errh = ErrorHandler::default_handler();
    return errh->error(
        "Flow Dispatcher (port %u): Flow isolation is supported since DPDK 17.08", port_id
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
FlowDispatcher::flow_rule_query(const uint32_t &int_rule_id, int64_t &matched_pkts, int64_t &matched_bytes)
{
    // Only active instances can query a NIC
    if (!active()) {
        _errh->error(
            "Flow Dispatcher (port %u): Inactive instance cannot query flow rule #%" PRIu32, _port_id, int_rule_id);
        return "";
    }

    struct rte_flow_error error;
    struct rte_port *port;
    struct port_flow *pf;
    struct rte_flow_action *action = 0;
    struct rte_flow_query_count query;

    port = get_port(_port_id);
    if (!port->flow_list || (flow_rules_count() == 0)) {
        _errh->message("Flow Dispatcher (port %u): No flow rules to query", _port_id);
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
            "Flow Dispatcher (port %u): No stats for invalid flow rule with ID %" PRIu32, _port_id, int_rule_id);
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
            "Flow Dispatcher (port %u): No count instruction for flow rule with ID %" PRIu32, _port_id, int_rule_id);
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
            "Flow Dispatcher (port %u): Failed to query stats for flow rule with ID %" PRIu32, _port_id, int_rule_id);
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
 * Reports NIC's aggregate flow rule statistics.
 *
 * @return NIC's aggregate flow rule statistics as a string
 */
String
FlowDispatcher::flow_rule_aggregate_stats()
{
    // Only active instances might have statistics
    if (!active()) {
        return "";
    }

    struct rte_port *port = get_port(_port_id);
    if (!port->flow_list || (flow_rules_count() == 0)) {
        _errh->warning("Flow Dispatcher (port %u): No aggregate statistics due to no traffic", _port_id);
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
        _errh->warning("Flow Dispatcher (port %u): No queues to produce aggregate statistics", _port_id);
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
FlowDispatcher::flow_rules_with_hits_count()
{
    // Only active instances might have statistics
    if (!active()) {
        return 0;
    }

    struct rte_port *port = get_port(_port_id);
    if (!port->flow_list || (flow_rules_count() == 0)) {
        _errh->warning("Flow Dispatcher (port %u): No counter for flow rules with hits due to no traffic", _port_id);
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
FlowDispatcher::flow_rules_count()
{
    return _flow_cache->get_rule_counter();
}

/**
 * Return the flow rule counter for a particular NIC
 * by traversing the list of flow rules.
 *
 * @return the number of flow rules being installed
 */
uint32_t
FlowDispatcher::flow_rules_count_explicit()
{
    // Only active instances might have some rules
    if (!active()) {
        return 0;
    }

    uint32_t rules_nb = 0;

    struct rte_port *port = get_port(_port_id);
    if (!port->flow_list) {
        if (verbose()) {
            _errh->message("Flow Dispatcher (port %u): No flow rules", _port_id);
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
FlowDispatcher::nic_and_cache_counts_agree()
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
FlowDispatcher::flow_rules_list(const bool only_matching_rules)
{
    if (!active()) {
        return "Flow Dispatcher is inactive";
    }

    struct rte_port *port = get_port(_port_id);
    if (!port->flow_list || (flow_rules_count() == 0)) {
        _errh->error("Flow Dispatcher (port %u): No flow rules to list", _port_id);
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
        if (_flow_cache->internal_rule_id_exists(id)) {
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
FlowDispatcher::flow_rule_ids_internal(const bool from_nic)
{
    if (!active()) {
        return "Flow Dispatcher is inactive";
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
FlowDispatcher::flow_rule_ids_internal_nic()
{
    struct rte_port *port = get_port(_port_id);
    if (!port->flow_list || (flow_rules_count() == 0)) {
        _errh->error("Flow Dispatcher (port %u): No flow rule IDs to list", _port_id);
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
FlowDispatcher::flow_rule_ids_internal_cache()
{
    if (!active()) {
        return "Flow Dispatcher is inactive";
    }

    Vector<uint32_t> rule_ids = _flow_cache->internal_rule_ids();
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
FlowDispatcher::flow_rule_ids_internal_counters()
{
    if (!active()) {
        return "Flow Dispatcher is inactive";
    }

    Vector<uint32_t> rule_ids = _flow_cache->internal_rule_ids_counters();
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
FlowDispatcher::flow_rule_ids_global()
{
    if (!active()) {
        return "Flow Dispatcher is inactive";
    }

    Vector<uint32_t> rule_ids = _flow_cache->global_rule_ids();
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
FlowDispatcher::flow_rules_sort(struct rte_port *port, struct port_flow **sorted_rules)
{
    if (!port || !port->flow_list) {
        _errh->error("Flow Dispatcher (port %u): Cannot sort empty flow rules' list", _port_id);
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
 * Flushes all of the flow rules from a NIC associated with this Flow Dispatcher instance.
 *
 * @return the number of flow rules being flushed
 */
uint32_t
FlowDispatcher::flow_rules_flush()
{
    // Only active instances can configure a NIC
    if (!active()) {
        _errh->message("Flow Dispatcher (port %u): Nothing to flush", _port_id);
        return 0;
    }

    RuleTiming rdts(_port_id);
    rdts.start = Timestamp::now_steady();

    uint32_t rules_before_flush = flow_rules_count_explicit();
    if (rules_before_flush == 0) {
        return 0;
    }

    // Successful flush means zero rules left
    if (port_flow_flush(_port_id) != FLOWDISP_SUCCESS) {
        uint32_t rules_after_flush = flow_rules_count_explicit();
        _errh->warning("Flow Dispatcher (port %u): Flushed only %" PRIu32 " rules", _port_id, (rules_before_flush - rules_after_flush));
        return (rules_before_flush - rules_after_flush);
    }

    rdts.end = Timestamp::now_steady();

    rdts.update(rules_before_flush);
    add_rule_del_stats(rdts);

    // Successful flush, now flush also the cache
    _flow_cache->flush_rules_from_cache();

    if (_verbose) {
        _errh->message(
            "Flow Dispatcher (port %u): Successfully flushed %" PRIu32 " rules in %.0f ms at the rate of %.3f rules/sec",
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
FlowDispatcher::filter_rule(String &rule)
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
FlowDispatcher::fetch_token_after_keyword(char *rule, const String &keyword)
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
FlowDispatcher::min_avg_max(float &min, float &mean, float &max, const bool install, const bool latency)
{
    const Vector<RuleTiming> *rule_stats_vec = 0;
    if (install) {
        rule_stats_vec = &_rule_inst_stats_map[_port_id];
    } else {
        rule_stats_vec = &_rule_del_stats_map[_port_id];
    }

    if (!rule_stats_vec) {
        _errh->warning("Flow Dispatcher (port %u): No rule statistics available", _port_id);
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
FlowDispatcher::rule_consistency_check(const int32_t &target_number_of_rules)
{
    if (target_number_of_rules < 0) {
        _errh->error(
            "Flow Dispatcher (port %u): Cannot verify consistency with a negative number of target rules",
            get_port_id(), target_number_of_rules
        );
        return;
    }

    // First check the flow rule cache
    _flow_cache->cache_consistency_check(target_number_of_rules);

    // Then the NIC with respect to the cache
    nic_consistency_check(target_number_of_rules);
}

/**
 * Performs a run-time consistency check with respect to the desired occupancy of the NIC.
 *
 * @args target_number_of_rules: desired NIC occupancy
 */
void
FlowDispatcher::nic_consistency_check(const int32_t &target_number_of_rules)
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
