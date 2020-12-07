// -*- c-basic-offset: 4 -*-
/*
 * flowrulemanager.hh -- Flow rule manager API for DPDK-based NICs, based on DPDK's Flow API
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

#ifndef CLICK_FLOWRULEMANAGER_H
#define CLICK_FLOWRULEMANAGER_H

#include <click/error.hh>
#include <click/hashmap.hh>
#include <click/hashtable.hh>
#include <click/dpdkdevice.hh>
#if RTE_VERSION >= RTE_VERSION_NUM(20,2,0,0)
    #include <click/flowrulecache.hh>
    #include <click/flowruleparser.hh>
#endif

CLICK_DECLS

/**
 * DPDK Flow Rule Manager API.
 */
#if RTE_VERSION >= RTE_VERSION_NUM(20,2,0,0)

class DPDKDevice;

class RuleTiming {
    public:
        RuleTiming(portid_t pt_id) : port_id(pt_id) {
        }

        ~RuleTiming() {
        }

        portid_t port_id;       // The NIC to measure
        uint32_t rules_nb;      // Log the number of rules being installed/deleted
        float latency_ms;       // Measure rule installation/deletion latency (ms)
        float rules_per_sec;    // Measure rule installation/deletion rate (rules/sec)
        Timestamp start, end;

        void update(const uint32_t &rules_nb) {
            this->rules_nb = rules_nb;
            this->latency_ms = (float) (end - start).usecval() / (float) 1000;
            this->rules_per_sec = (rules_nb > 0) ? (float) (rules_nb * 1000) / this->latency_ms : 0;
        }
};

class FlowRuleManager {
    public:
        FlowRuleManager();
        FlowRuleManager(portid_t port_id, ErrorHandler *errh);
        ~FlowRuleManager();

        // DPDKDevice mode for the Flow Rule Manager
        static String DISPATCHING_MODE;

        // Supported Flow Rule Manager handlers (called from FromDPDKDevice)
        static String FLOW_RULE_ADD;
        static String FLOW_RULE_DEL;
        static String FLOW_RULE_IDS_GLB;
        static String FLOW_RULE_IDS_INT;
        static String FLOW_RULE_PACKET_HITS;
        static String FLOW_RULE_BYTE_COUNT;
        static String FLOW_RULE_AGGR_STATS;
        static String FLOW_RULE_LIST;
        static String FLOW_RULE_LIST_WITH_HITS;
        static String FLOW_RULE_COUNT;
        static String FLOW_RULE_COUNT_WITH_HITS;
        static String FLOW_RULE_ISOLATE;
        static String FLOW_RULE_FLUSH;

        // For debugging
        static bool DEF_VERBOSITY;
        static bool DEF_DEBUG_MODE;

        // Set of flow rule items supported by the Flow API
        static HashMap<int, String> flow_item;

        // Set of flow rule actions supported by the Flow API
        static HashMap<int, String> flow_action;

        // Global table of DPDK ports mapped to their Flow Rule Manager objects
        static HashTable<portid_t, FlowRuleManager *> dev_flow_rule_mgr;

        // Map of ports to Flow Rule Manager instances
        static HashTable<portid_t, FlowRuleManager *> flow_rule_manager_map();

        // Cleans the mappings between ports and Flow Rule Manager instances
        static void clean_flow_rule_manager_map();

        // Acquires a Flow Rule Manager instance on a port
        static FlowRuleManager *get_flow_rule_mgr(const portid_t &port_id, ErrorHandler *errh = NULL);

        // Parser initialization
        static struct cmdline *flow_rule_parser(ErrorHandler *errh);

        // Get a flow rule cache associated with a Flow Rule Manager
        FlowRuleCache *flow_rule_cache();

        // Deletes the error handler of this element
        inline void delete_error_handler() { if (_errh) delete _errh; };

        // Port ID handlers
        inline void set_port_id(const portid_t &port_id) {
            _port_id = port_id;
        };
        inline portid_t get_port_id() { return _port_id; };

        // Activation/deactivation handlers
        inline void set_active(const bool &active) {
            _active = active;
        };
        inline bool active() { return _active; };

        // Verbosity handlers
        inline void set_verbose(const bool &verbose) {
            _verbose = verbose;
        };
        inline bool verbose() { return _verbose; };

        inline void set_debug_mode(const bool &debug_mode) {
            _debug_mode = debug_mode;
        };
        inline bool debug_mode() { return _debug_mode; };

        // Rules' file handlers
        inline void set_rules_filename(const String &file) {
            _rules_filename = file;
        };
        inline String rules_filename() { return _rules_filename; };

        // Calibrates flow rule cache before inserting new rules
        void flow_rule_cache_calibrate(const uint32_t *int_rule_ids, const uint32_t &rules_nb);
        void flow_rule_cache_calibrate(const HashMap<uint32_t, String> &rules_map);

        // Install NIC flow rules from a file
        int32_t flow_rules_add_from_file(const String &filename);

        // Update NIC flow rules
        int32_t flow_rules_update(const HashMap<uint32_t, String> &rules_map, bool by_controller = true, int core_id = -1);

        // Loads a set of rules from a file to memory
        String flow_rules_from_file_to_string(const String &filename);

        // Install flow rule(s) in a NIC
        int flow_rules_install(const String &rules, const uint32_t &rules_nb, const bool verbose=true);
        int flow_rule_install(
            const uint32_t &int_rule_id, const uint32_t &rule_id,
            const int &core_id, const String &rule,
            const bool with_cache = true,
	    const bool verbose=true
        );

        // Verify the presence/absence of a list of rules in/from the NIC
        int flow_rules_verify(const Vector<uint32_t> &int_rule_ids_vec, const Vector<uint32_t> &old_int_rule_ids_vec);
        int flow_rules_verify_presence(const Vector<uint32_t> &int_rule_ids_vec);
        int flow_rules_verify_absence(const Vector<uint32_t> &old_int_rule_ids_vec);

        // Return a flow rule object with a specific ID
        struct port_flow *flow_rule_get(const uint32_t &int_rule_id);

        // Delete a batch of flow rules from a NIC
        int32_t flow_rules_delete(const Vector<uint32_t> &old_int_rule_ids_vec, const bool with_cache = true);
        int32_t flow_rules_delete(uint32_t *int_rule_ids, const uint32_t &rules_nb, const bool with_cache = true);

        // Query flow rule statistics
        String flow_rule_query(const uint32_t &int_rule_id, int64_t &matched_pkts, int64_t &matched_bytes);

        // Query aggregate flow rule statistics
        String flow_rule_aggregate_stats();

        // Counts the number of rules with hits in a NIC
        uint32_t flow_rules_with_hits_count();

        // Counts the number of rules in a NIC (using the cache)
        uint32_t flow_rules_count();

        // Counts the number of rules in a NIC (in hardware)
        uint32_t flow_rules_count_explicit();

        // Compares NIC and cache rule counts and asserts inconsistency
        void nic_and_cache_counts_agree();

        // Lists all NIC flow rules
        String flow_rules_list(const bool only_matching_rules = false);

        // Lists all installed (internal + global flow rule IDs along with counters
        String flow_rule_ids_internal_counters();
        String flow_rule_ids_internal_cache();
        String flow_rule_ids_internal_nic();
        String flow_rule_ids_internal(const bool from_nic = true);
        String flow_rule_ids_global();

        // Flush all of the rules from a NIC
        uint32_t flow_rules_flush();

        // Verify the consistency of the NIC and Flow Cache upon a rule update
        void flow_rule_consistency_check(const int32_t &target_number_of_rules);

        // Filters unwanted components from rule
        static bool flow_rule_filter(String &rule);

        // Returns a rule token after an input keyword
        static String fetch_token_after_keyword(char *rule, const String &keyword);

        // Returns statistics related to rule installation/deletion
        void min_avg_max(float &min, float &mean, float &max, const bool install = true, const bool latency = true);

        // Methods to access per port rule installation/deletion statistics
        static inline void add_rule_inst_stats(const RuleTiming &rits) {
            Vector<RuleTiming> *rule_stats_vec = _rule_inst_stats_map.findp(rits.port_id);
            if (!rule_stats_vec) {
                Vector<RuleTiming> new_vec;
                new_vec.push_back(rits);
                _rule_inst_stats_map.insert(rits.port_id, new_vec);
            } else {
                rule_stats_vec->push_back(rits);
            }
        }
        static inline void add_rule_del_stats(const RuleTiming &rdts) {
            Vector<RuleTiming> *rule_stats_vec = _rule_del_stats_map.findp(rdts.port_id);
            if (!rule_stats_vec) {
                Vector<RuleTiming> new_vec;
                new_vec.push_back(rdts);
                _rule_del_stats_map.insert(rdts.port_id, new_vec);
            } else {
                rule_stats_vec->push_back(rdts);
            }
        }

    private:
        // Device ID
        portid_t _port_id;

        // Indicates whether Flow Rule Manager is active for a given device
        bool _active;

        // Set stdout verbosity
        bool _verbose;
        bool _debug_mode;

        // Filename that contains the rules to be installed
        String _rules_filename;

        // A dedicated error handler
        ErrorVeneer *_errh;

        // A flow rule cache associated with the port of this Flow Rule Manager
        FlowRuleCache *_flow_rule_cache;

        // Isolated mode guarantees that all ingress traffic comes from defined flow rules only (current and future)
        static HashMap<portid_t, bool> _isolated;

        // Flow rule commands' parser
        static struct cmdline *_parser;

        // Map of ports to their rule installation/deletion statistics
        static HashMap<portid_t, Vector<RuleTiming>> _rule_inst_stats_map;
        static HashMap<portid_t, Vector<RuleTiming>> _rule_del_stats_map;

        // Restrict ingress traffic to the defined flow rules
        static int flow_rules_isolate(const portid_t &port_id, const int &set);

        // Pre-populate the supported matches and actions on relevant maps
        void populate_supported_flow_items_and_actions();

        // Sorts a list of flow rules by group, priority, and ID
        void flow_rules_sort(struct rte_port *port, struct port_flow **sorted_rules);

        // Verify that the NIC has the right number of rules
        void nic_consistency_check(const int32_t &target_number_of_rules);

};

#endif /* RTE_VERSION >= RTE_VERSION_NUM(20,2,0,0) */

CLICK_ENDDECLS

#endif /* CLICK_FLOWRULEMANAGER_H */
