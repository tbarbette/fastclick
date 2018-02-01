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

    // DPDKDevice mode is Flow Director
    static String FLOW_DIR_MODE;

    // Supported flow director handlers (called from FromDPDKDevice)
    static String FLOW_RULE_ADD;
    static String FLOW_RULE_DEL;
    static String FLOW_RULE_LIST;
    static String FLOW_RULE_FLUSH;

    // For debugging
    static bool DEF_VERBOSITY;

    // Global table of ports mapped to their Flow Director objects
    static HashTable<portid_t, FlowDirector *> _dev_flow_dir;

    inline static void delete_error_handler() { delete _errh; };

    // Port ID handlers
    inline void set_port_id(const portid_t &port_id) {
        _port_id = port_id;
    };
    inline portid_t get_port_id() { return _port_id; };

    // Activation/deactivation handlers
    inline void set_active(const bool &active) {
        _active = active;
        _errh->message(
            "Flow Director (port %u): State active",
            _port_id
        );
    };
    inline bool get_active() { return _active; };

    // Verbosity handlers
    inline void set_verbose(const bool &verbose) {
        _verbose = verbose;
    };
    inline bool get_verbose() { return _verbose; };

    // Rules' file handlers
    inline void set_rules_filename(const String &file) {
        _rules_filename = file;
    };
    inline String get_rules_filename() { return _rules_filename; };

    // Parser initialization
    static struct cmdline *get_parser(ErrorHandler *errh);

    // Manages the Flow Director instances
    static FlowDirector *get_flow_director(
        const portid_t &port_id,
        ErrorHandler   *errh
    );

    // Add flow rules to a NIC from a file
    static int add_rules_from_file(
        const portid_t &port_id,
        const String   &filename
    );

    // Install a flow rule into a NIC
    static bool flow_rule_install(
        const portid_t &port_id,
        const uint32_t &rule_id,
        const char     *rule
    );

    // Return a flow rule object with a specific ID
    static struct port_flow *flow_rule_get(
        const portid_t &port_id,
        const uint32_t &rule_id
    );

    // Delete a flow rule from a NIC
    static bool flow_rule_delete(
        const portid_t &port_id,
        const uint32_t &rule_id
    );

    // Counts the number of rules in a NIC
    static uint32_t flow_rules_count(const portid_t &port_id);

    // Flush all of the rules from a NIC
    static uint32_t flow_rules_flush(const portid_t &port_id);

private:

    // Device ID
    portid_t _port_id;

    // Indicates whether Flow Director is active for a given device
    bool _active;

    // Set stdout verbosity
    bool _verbose;

    // Filename that contains the rules to be installed
    String _rules_filename;

    // Flow rule commands' parser
    static struct cmdline *_parser;

    // Flow rule counter per device
    static HashTable<portid_t, uint32_t> _rules_nb;

    // A unique error handler
    static ErrorVeneer *_errh;

    static uint32_t flow_rules_count_explicit(
        const portid_t &port_id
    );
};

#endif /* RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0) */

CLICK_ENDDECLS

#endif /* CLICK_FLOWDIRECTOR_H */