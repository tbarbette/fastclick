// -*- c-basic-offset: 4 -*-
/*
 * flowrulecache.hh -- A flow rule cache API for DPDK's Flow API
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

#ifndef CLICK_FLOWRULECACHE_H
#define CLICK_FLOWRULECACHE_H

#include <click/error.hh>
#include <click/hashmap.hh>
#include <click/dpdkdevice.hh>

CLICK_DECLS

/**
 * Flow Rule Cache API.
 */
#if RTE_VERSION >= RTE_VERSION_NUM(20,2,0,0)

class FlowRuleCache {
    public:
        FlowRuleCache(portid_t port_id, bool verbose, bool debug_mode, ErrorHandler *errh)
            : _port_id(port_id), _rules_nb(0), _next_rule_id(0),
              _rules(), _internal_rule_map(), _matched_pkts(), _matched_bytes() {
            assert(_port_id >= 0);
            _errh = new ErrorVeneer(errh);
            assert(_errh);
            _verbose = verbose;
            _debug_mode = debug_mode;
        }

        ~FlowRuleCache() {
            flush_rules_from_cache();
        }

        // Device methods
        portid_t get_port_id();
        String get_device_address();

        // Search methods
        bool has_rules();
        bool global_rule_id_exists(const uint32_t &rule_id);
        bool internal_rule_id_exists(const uint32_t &int_rule_id);
        uint32_t global_from_internal_rule_id(const uint32_t &int_rule_id);
        int32_t internal_from_global_rule_id(const uint32_t &rule_id);
        template<typename T> void sort_rule_ids_inc(Vector<T> &rule_ids_vec);
        template<typename T> void sort_rule_ids_dec(Vector<T> &rule_ids_vec);
        Vector<uint32_t> global_rule_ids(const bool increasing = true);
        Vector<uint32_t> internal_rule_ids(const bool increasing = true);
        Vector<uint32_t> internal_rule_ids_counters(const bool increasing = true);
        HashMap<uint32_t, String> *rules_map_by_core_id(const int &core_id);
        Vector<String> rules_list_by_core_id(const int &core_id);
        Vector<int> cores_with_rules();
        String get_rule_by_global_id(const uint32_t &rule_id);
        String get_rule_by_internal_id(const uint32_t &int_rule_id);

        // Flow Rule Cache methods
        int32_t currently_max_internal_rule_id();
        uint32_t next_internal_rule_id();
        void set_next_internal_rule_id(uint32_t next_id);
        int insert_rule_in_flow_cache(
            const int &core_id, const uint32_t &rule_id,
            const uint32_t &int_rule_id, const String rule
        );
        bool update_rule_in_flow_cache(
            const int &core_id, const uint32_t &rule_id,
            const uint32_t &int_rule_id, String rule
        );
        int32_t delete_rule_by_global_id(const uint32_t &rule_id);
        String delete_rules_by_internal_id(const uint32_t *int_rule_ids, const uint32_t &rules_nb);
        String delete_rules_by_internal_id(const Vector<String> &rules_vec);
        int32_t flush_rules_from_cache();

        // Flow Rule Cache monitoring methods
        void set_matched_packets(const uint32_t &int_rule_id, uint64_t value);
        uint64_t get_matched_packets(const uint32_t &int_rule_id);
        void set_matched_bytes(const uint32_t &int_rule_id, uint64_t value);
        uint64_t get_matched_bytes(const uint32_t &int_rule_id);
        inline uint32_t get_rule_counter() { return _rules_nb; };
        void initialize_rule_counters(uint32_t *int_rule_ids, const uint32_t &rules_nb);
        void delete_rule_counters(uint32_t *int_rule_ids, const uint32_t &rules_nb);
        void cache_consistency_check(const int32_t &target_number_of_rules);
        void correlate_candidate_id_with_cache(int32_t &candidate);
        void flush_rule_counters();

    private:
        // NIC's port ID associated with this Flow Rule Cache
        portid_t _port_id;

        // Flow rules' counter
        uint32_t _rules_nb;

        // Next available rule ID
        uint32_t _next_rule_id;

        // Maps CPU cores to a map of global rule IDs -> rules
        HashMap<int, HashMap<uint32_t, String> *> _rules;

        // Maps global rule IDs to internal NIC rule IDs
        HashMap<uint32_t, uint32_t> _internal_rule_map;

        // Matched packets and bytes per rule ID
        HashMap<uint32_t, uint64_t> _matched_pkts;
        HashMap<uint32_t, uint64_t> _matched_bytes;

        // An error handler
        ErrorVeneer *_errh;

        // Set stdout verbosity
        bool _verbose;
        bool _debug_mode;

        // Methods to facilitate the mapping between ONOS and NIC rule IDs
        bool store_rule_id_mapping(const uint32_t &rule_id, const uint32_t &int_rule_id);
        bool delete_rule_id_mapping(const uint32_t &rule_id);

        // Methods to verify cache consistency
        bool verify_transactions(const Vector<uint32_t> &int_vec, const Vector<uint32_t> &glb_vec);
};

/**
 * Sorts a list of flow rule IDs by increasing order.
 *
 * @arg rule_ids_vec: a list of rule IDs to sort
 */
template<typename T>
void
FlowRuleCache::sort_rule_ids_inc(Vector<T> &rule_ids_vec)
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
 * Sorts a a list of flow rule IDs by decreasing order.
 *
 * @arg rule_ids_vec: a list of rule IDs to sort
 */
template<typename T>
void
FlowRuleCache::sort_rule_ids_dec(Vector<T> &rule_ids_vec)
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

#endif /* RTE_VERSION >= RTE_VERSION_NUM(20,2,0,0) */

CLICK_ENDDECLS

#endif /* CLICK_FLOWRULECACHE_H */
