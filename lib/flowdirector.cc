// -*- c-basic-offset: 4; related-file-name: "flowdirector.hh" -*-
/*
 * flowdirector.cc -- library for integrating DPDK's Flow Director in Click
 *
 * Copyright (c) 2018 Georgios Katsikas, RISE SICS AB
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
#include <click/flowdirector.hh>

CLICK_DECLS

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)

/**
 * Flow Director implementation.
 */

// DPDKDevice mode is Flow Director
String FlowDirector::FLOW_DIR_MODE   = "flow_dir";

// Supported flow director handlers (called from FromDPDKDevice)
String FlowDirector::FLOW_RULE_ADD   = "add_rule";
String FlowDirector::FLOW_RULE_DEL   = "del_rule";
String FlowDirector::FLOW_RULE_LIST  = "count_rules";
String FlowDirector::FLOW_RULE_FLUSH = "flush_rules";

// Flow rule counter per device
HashTable<portid_t, uint32_t> FlowDirector::_rules_nb;

// Default verbosity setting
bool FlowDirector::DEF_VERBOSITY = false;

// Global table of DPDK ports mapped to their Flow Director objects
HashTable<portid_t, FlowDirector *> FlowDirector::_dev_flow_dir;

// A unique parser
struct cmdline *FlowDirector::_parser = NULL;

// A unique error handler
ErrorVeneer *FlowDirector::_errh;

FlowDirector::FlowDirector() :
        _port_id(-1), _active(false),
        _verbose(DEF_VERBOSITY)
{
    _errh = new ErrorVeneer(ErrorHandler::default_handler());
}

FlowDirector::FlowDirector(portid_t port_id, ErrorHandler *errh) :
        _port_id(port_id), _active(false),
        _verbose(DEF_VERBOSITY)
{
    _errh = new ErrorVeneer(errh);
    _rules_nb[port_id] = 0;

    if (_verbose) {
        _errh->message(
            "Flow Director (port %u): Created (state inactive)",
            _port_id
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
        if (_verbose) {
            _errh->message(
                "Flow Director (port %u): Parser deleted", _port_id
            );
        }
    }

    // Clean up flow rule counters
    if (!_rules_nb.empty()) {
        _rules_nb.clear();
    }

    if (_verbose) {
        _errh->message(
            "Flow Director (port %u): Destroyed", _port_id
        );
    }
}

/**
 * Obtains an instance of the Flow Director parser.
 *
 * @param errh an instance of the error handler
 * @return a Flow Director parser object
 */
struct cmdline *
FlowDirector::get_parser(ErrorHandler *errh)
{
    if (!_parser) {
        return flow_parser_init(errh);
    }

    return _parser;
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
    // Invalid port ID
    if (port_id >= rte_eth_dev_count()) {
        click_chatter(
            "Flow Director (port %u): Denied to create instance "
            "for invalid port", port_id
        );
        return 0;
    }

    // Get the Flow Director of the desired port
    FlowDirector *flow_dir = _dev_flow_dir.get(port_id);

    // Not there, let's created it
    if (!flow_dir) {
        flow_dir = new FlowDirector(port_id, errh);
        _dev_flow_dir[port_id] = flow_dir;
    }

    // Create a Flow Director parser
    _parser = get_parser(errh);

    // Ship it back
    return flow_dir;
}

/**
 * Installs a set of string-based rules read from a file.
 * Allowed rule types: create and delete.
 *
 * @param port_id the ID of the NIC
 * @param filename the file that contains the rules
 */
int
FlowDirector::add_rules_from_file(
        const portid_t &port_id,
        const String   &filename)
{
    uint32_t rule_no = 0;

    FILE *fp = NULL;
    fp = fopen(filename.c_str(), "r");
    if (fp == NULL) {
        return _errh->error(
            "Flow Director (port %u): Failed to open file '%s'",
            port_id, filename.c_str());
    }

    char  *line = NULL;
    size_t  len = 0;

    // Read file line-by-line (or rule-by-rule)
    while ((getline(&line, &len, fp)) != -1) {
        // Filter out irrelevant DPDK commands
        if (!strstr (line, "create") && !strstr (line, "delete")) {
            _errh->warning(
                "Flow Director (port %u): "
                "Expects only create and/or delete rules", port_id
            );
            continue;
        }

        if (FlowDirector::flow_rule_install(port_id, rule_no++, line)) {
            _rules_nb[port_id]++;
        }
    }

    // Close the file
    fclose(fp);

    // Free memory
    if (line) {
        free(line);
    }

    _errh->message(
        "Flow Director (port %u): %u/%u rules are installed",
        port_id, _rules_nb[port_id], rule_no
    );
}

/**
 * Translates a string-based rule into a flow rule
 * object and install it to a NIC.
 *
 * @param port_id the ID of the NIC
 * @param rule_id a flow rule's ID
 * @param rule a flow rule as a string
 * @return a flow rule object
 */
bool
FlowDirector::flow_rule_install(
        const portid_t &port_id,
        const uint32_t &rule_id,
        const char *rule)
{
    // Only active instances can configure a NIC
    if (!_dev_flow_dir[port_id]->get_active()) {
        return false;
    }

    int res = flow_parser_parse(_parser, (char *) rule, _errh);
    if (res == FLOWDIR_ERROR) {
        _errh->error(
            "Flow Director (port %u): Failed to parse rule #%4u",
            port_id, rule_id
        );
        return false;
    }

    return true;
}

/**
 * Returns a flow rule object of a specific port with a specific ID.
 *
 * @param port_id the ID of the NIC
 * @param rule_id a rule ID
 * @return a flow rule object
 */
struct port_flow *
FlowDirector::flow_rule_get(
        const portid_t &port_id,
        const uint32_t &rule_id)
{
    struct rte_port  *port;
    struct port_flow *pf;

    port = get_port(port_id);
    if (!port->flow_list) {
        return NULL;
    }

    for (pf = port->flow_list; pf != NULL; pf = pf->next) {
        if (pf->id == rule_id) {
            return pf;
        }
    }

    return NULL;
}

/**
 * Removes a flow rule object from the NIC.
 *
 * @param port_id the ID of the NIC
 * @param rule_id a flow rule's ID
 * @return status
 */
bool
FlowDirector::flow_rule_delete(
        const portid_t &port_id,
        const uint32_t &rule_id)
{
    // Only active instances can configure a NIC
    if (!_dev_flow_dir[port_id]->get_active()) {
        return false;
    }

    const uint32_t rules_to_delete[] = {rule_id};

    if (port_flow_destroy(port_id, 1, rules_to_delete) == FLOWDIR_SUCCESS) {
        _rules_nb[port_id]--;
        return true;
    }

    return false;
}

/**
 * Return the explicit rule counter for a particular NIC.
 *
 * @param port_id the ID of the NIC
 * @return the number of rules being installed
 */
uint32_t
FlowDirector::flow_rules_count(const portid_t &port_id)
{
    return _rules_nb[port_id];
}

/**
 * Counts all of the rules installed to a NIC
 * by traversing the list of rules.
 *
 * @param port_id the ID of the NIC
 * @return the number of rules being installed
 */
uint32_t
FlowDirector::flow_rules_count_explicit(const portid_t &port_id)
{
    // Only active instances might have some rules
    if (!_dev_flow_dir[port_id]->get_active()) {
        return 0;
    }

    struct rte_port  *port;
    struct port_flow *pf;
    uint32_t rules_nb = 0;

    port = get_port(port_id);
    if (!port->flow_list) {
        return 0;
    }

    /* Sort flows by group, priority and ID. */
    for (pf = port->flow_list; pf != NULL; pf = pf->next) {
        rules_nb++;
    }

    // Consistency
    assert(_rules_nb[port_id] == rules_nb);

    return rules_nb;
}

/**
 * Flushes all of the rules from a NIC.
 *
 * @param port_id the ID of the NIC
 * @return the number of rules being flushed
 */
uint32_t
FlowDirector::flow_rules_flush(const portid_t &port_id)
{
    // Only active instances can configure a NIC
    if (!_dev_flow_dir[port_id]->get_active()) {
        if (_dev_flow_dir[port_id]->get_verbose()) {
            _errh->message(
                "Flow Director (port %u): Nothing to flush", port_id
            );
        }
        return 0;
    }

    uint32_t rules_before_flush = _rules_nb[port_id];

    // Successful flush means zero rules left
    if (port_flow_flush(port_id) == FLOWDIR_SUCCESS) {
        _rules_nb[port_id] = 0;
        return rules_before_flush;
    }

    // Otherwise, count to see how many of them are there
    return flow_rules_count_explicit(port_id);
}

#endif /* RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0) */

CLICK_ENDDECLS
