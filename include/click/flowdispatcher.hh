// -*- c-basic-offset: 4 -*-
/*
 * flowdispatcher.hh -- DPDK's Flow API in Click
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

#ifndef CLICK_FLOWDISPATCHER_H
#define CLICK_FLOWDISPATCHER_H

#include <click/error.hh>
#include <click/hashmap.hh>
#include <click/hashtable.hh>
#include <click/dpdkdevice.hh>
#if RTE_VERSION >= RTE_VERSION_NUM(20,2,0,0)
    #include <click/flowdispatcherparser.hh>
#endif

CLICK_DECLS

/**
 * DPDK's Flow API.
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

class FlowCache {
    public:
        FlowCache(portid_t port_id, bool verbose, bool debug_mode, ErrorHandler *errh)
            : _port_id(port_id), _rules_nb(0), _next_rule_id(0),
              _rules(), _internal_rule_map(), _matched_pkts(), _matched_bytes() {
            assert(_port_id >= 0);
            _errh = new ErrorVeneer(errh);
            assert(_errh);
            _verbose = verbose;
            _debug_mode = debug_mode;
        }

        ~FlowCache() {
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

        // Flow Cache methods
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

        // Flow Cache monitoring methods
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
        // NIC's port ID associated with this flow cache
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

class FlowDispatcher {
    public:
        FlowDispatcher();
        FlowDispatcher(portid_t port_id, ErrorHandler *errh);
        ~FlowDispatcher();

        // DPDKDevice mode is Flow Dispatcher
        static String DISPATCHING_MODE;

        // Supported Flow Dispatcher handlers (called from FromDPDKDevice)
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

        // Global table of DPDK ports mapped to their Flow Dispatcher objects
        static HashTable<portid_t, FlowDispatcher *> dev_flow_disp;

        // Map of ports to Flow Dispatcher instances
        static HashTable<portid_t, FlowDispatcher *> flow_dispatcher_map();

        // Cleans the mappings between ports and Flow Dispatcher instances
        static void clean_flow_dispatcher_map();

        // Acquires a Flow Dispatcher instance on a port
        static FlowDispatcher *get_flow_dispatcher(const portid_t &port_id, ErrorHandler *errh = NULL);

        // Parser initialization
        static struct cmdline *parser(ErrorHandler *errh);

        // Get the flow cache associated with a Flow Dispatcher
        FlowCache *get_flow_cache();

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

        // Flow rules' isolation mode
        static inline void set_isolation_mode(const portid_t &port_id, const bool &isolated) {
            _isolated.insert(port_id, isolated);
            if (_isolated[port_id]) {
                flow_rules_isolate(port_id, 1);
            } else {
                flow_rules_isolate(port_id, 0);
            }
        };
        static inline bool isolated(const portid_t &port_id) { return _isolated[port_id]; };

        // Calibrates flow rule cache before inserting new rules
        void calibrate_cache(const uint32_t *int_rule_ids, const uint32_t &rules_nb);
        void calibrate_cache(const HashMap<uint32_t, String> &rules_map);

        // Updates the internal ID for the next rule to be allocated
        void update_internal_rule_id();

        // Install NIC flow rules from a file
        int32_t add_rules_from_file(const String &filename);

        // Update NIC flow rules
        int32_t update_rules(const HashMap<uint32_t, String> &rules_map, bool by_controller = true, int core_id = -1);

        // Loads a set of rules from a file to memory
        String load_rules_from_file_to_string(const String &filename);

        // Install flow rule(s) in a NIC
        int flow_rules_install(const String &rules, const uint32_t &rules_nb);
        int flow_rule_install(
            const uint32_t &int_rule_id, const uint32_t &rule_id,
            const int &core_id, const String &rule,
            const bool with_cache = true
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
        void rule_consistency_check(const int32_t &target_number_of_rules);

        // Filters unwanted components from rule
        static bool filter_rule(String &rule);

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

        // Indicates whether Flow Dispatcher is active for a given device
        bool _active;

        // Set stdout verbosity
        bool _verbose;
        bool _debug_mode;

        // Filename that contains the rules to be installed
        String _rules_filename;

        // A dedicated error handler
        ErrorVeneer *_errh;

        // A low rule cache associated with the port of this Flow Dispatcher
        FlowCache *_flow_cache;

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

#endif /* RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0) */

CLICK_ENDDECLS

#endif /* CLICK_FLOWDISPATCHER_H */
