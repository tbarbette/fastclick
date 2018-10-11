// -*- c-basic-offset: 4 -*-
/*
 * flowdirector.hh -- Flow Director's API in Click
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

#ifndef CLICK_FLOWDIRECTOR_H
#define CLICK_FLOWDIRECTOR_H

#include <click/error.hh>
#include <click/hashmap.hh>
#include <click/hashtable.hh>
#include <click/dpdkdevice.hh>
#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
    #include <click/flowdirectorparser.hh>
#endif

CLICK_DECLS

/**
 * DPDK's Flow Director API.
 */
#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)

class DPDKDevice;

class FlowDirector {

public:
    FlowDirector();
    FlowDirector(portid_t port_id, ErrorHandler *errh);
    ~FlowDirector();

    // Return status
    static int ERROR;
    static int SUCCESS;

    // DPDKDevice mode is Flow Director
    static String FLOW_DIR_MODE;

    // Supported flow director handlers (called from FromDPDKDevice)
    static String FLOW_RULE_ADD;
    static String FLOW_RULE_DEL;
    static String FLOW_RULE_STATS;
    static String FLOW_RULE_LIST;
    static String FLOW_RULE_COUNT;
    static String FLOW_RULE_FLUSH;

    // For debugging
    static bool DEF_VERBOSITY;

    // Set of flow rule items supported by the Flow API
    static HashMap<int, String> _flow_item;

    // Set of flow rule actions supported by the Flow API
    static HashMap<int, String> _flow_action;

    // Global table of DPDK ports mapped to their Flow Director objects
    static HashTable<portid_t, FlowDirector *> _dev_flow_dir;

    // Map of ports to Flow Director instances
    static HashTable<portid_t, FlowDirector *> flow_director_map();

    // Cleans the mappings between ports and Flow Director instances
    static void clean_flow_director_map();

    // Acquires a Flow Director instance on a port
    static FlowDirector *get_flow_director(
        const portid_t &port_id,
        ErrorHandler   *errh = NULL
    );

    // Parser initialization
    static struct cmdline *parser(ErrorHandler *errh);

    // Deletes the error handler of this element
    inline void delete_error_handler() { if (_errh) delete _errh; };

    // Port ID handlers
    inline void set_port_id(const portid_t &port_id) {
        _port_id = port_id;
    };
    inline portid_t port_id() { return _port_id; };

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

    // Rules' file handlers
    inline void set_rules_filename(const String &file) {
        _rules_filename = file;
    };
    inline String rules_filename() { return _rules_filename; };

    // Returns the next rule ID to use for insertion on a port
    inline uint32_t next_unique_rule_id() {
        return _next_rule_id[_port_id];
    }

    // Install flow rule in a NIC from a file
    int add_rules_from_file(const String &filename);

    // Install a flow rule in a NIC
    int flow_rule_install(const uint32_t &rule_id, const String &rule);

    // Return a flow rule object with a specific ID
    struct port_flow *flow_rule_get(const uint32_t &rule_id);

    // Delete a flow rule from a NIC
    int flow_rule_delete(const uint32_t &rule_id);

    // Query flow rule statistics
    String flow_rule_query(const uint32_t &rule_id, int64_t &matched_pkts, int64_t &matched_bytes);
    String flow_rule_aggregate_stats();

    // Counts the number of rules in a NIC
    // Local memory counter
    uint32_t flow_rules_count();
    // NIC counter
    uint32_t flow_rules_count_explicit();

    // Lists all NIC rules
    String flow_rules_list();

    // Flush all of the rules from a NIC
    uint32_t flow_rules_flush();

private:

    // Device ID
    portid_t _port_id;

    // Indicates whether Flow Director is active for a given device
    bool _active;

    // Set stdout verbosity
    bool _verbose;

    // Filename that contains the rules to be installed
    String _rules_filename;

    // A dedicated error handler
    ErrorVeneer *_errh;

    // Flow rule commands' parser
    static struct cmdline *_parser;

    // Flow rule counter per device
    static HashTable<portid_t, uint32_t> _rules_nb;

    // Next rule ID per device
    static HashTable<portid_t, uint32_t> _next_rule_id;

    // Matched packets and bytes per rule ID per device
    static HashTable<portid_t, HashTable<uint32_t, uint64_t>> _matched_pkts;
    static HashTable<portid_t, HashTable<uint32_t, uint64_t>> _matched_bytes;

    // Pre-populate the supported matches and actions on relevant maps
    void populate_supported_flow_items_and_actions();

    // Filters unwanted components from rule
    bool filter_rule(char **rule);

    // Reset the flow rule counters on a given port
    bool flush_rule_counters_on_port();

};

#endif /* RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0) */

CLICK_ENDDECLS

#endif /* CLICK_FLOWDIRECTOR_H */
