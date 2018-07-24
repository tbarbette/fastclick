// -*- c-basic-offset: 4; related-file-name: "flowdirector.hh" -*-
/*
 * flowdirector.cc -- library for integrating DPDK's Flow Director in Click
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

// Next rule ID per device
HashTable<portid_t, uint32_t> FlowDirector::_next_rule_id;

// Default verbosity setting
bool FlowDirector::DEF_VERBOSITY = false;

// Global table of DPDK ports mapped to their Flow Director objects
HashTable<portid_t, FlowDirector *> FlowDirector::_dev_flow_dir;

// A unique parser
struct cmdline *FlowDirector::_parser = NULL;

// Error handling
int FlowDirector::ERROR = -1;
int FlowDirector::SUCCESS = 0;

FlowDirector::FlowDirector() :
        _port_id(-1), _active(false),
        _verbose(DEF_VERBOSITY), _rules_filename("")
{
    _errh = new ErrorVeneer(ErrorHandler::default_handler());
}

FlowDirector::FlowDirector(portid_t port_id, ErrorHandler *errh) :
        _port_id(port_id), _active(false),
        _verbose(DEF_VERBOSITY), _rules_filename("")
{
    _errh = new ErrorVeneer(errh);
    _rules_nb[_port_id] = 0;
    _next_rule_id[_port_id] = 0;

    if (verbose()) {
        _errh->message(
            "Flow Director (port %u): Created (state %s)",
            _port_id, _active ? "active" : "inactive"
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
        if (verbose()) {
            _errh->message(
                "Flow Director (port %u): Parser deleted", _port_id
            );
        }
    }

    // Clean up flow rule counters
    if (!_rules_nb.empty()) {
        _rules_nb.clear();
    }

    if (!_next_rule_id.empty()) {
        _next_rule_id.clear();
    }

    if (verbose()) {
        _errh->message(
            "Flow Director (port %u): Destroyed", _port_id
        );
    }

    delete_error_handler();
}

/**
 * Obtains an instance of the Flow Director parser.
 *
 * @param errh an instance of the error handler
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
 * Returns the global map of DPDK ports to
 * their Flow Director instances.
 *
 * @return a Flow Director instance map
 */
HashTable<portid_t, FlowDirector *>
FlowDirector::flow_director_map()
{
    return _dev_flow_dir;
}

/**
 * Cleans the global map of DPDK ports to
 * their Flow Director instances.
 */
void
FlowDirector::clean_flow_director_map()
{
    if (!_dev_flow_dir.empty()) {
        _dev_flow_dir.clear();
    }
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
    if (!errh) {
        errh = ErrorHandler::default_handler();
    }

    // Invalid port ID
    if (port_id >= DPDKDevice::dev_count()) {
        click_chatter(
            "Flow Director (port %u): Denied to create instance "
            "for invalid port", port_id
        );
        return NULL;
    }

    // Get the Flow Director of the desired port
    FlowDirector *flow_dir = _dev_flow_dir.get(port_id);

    // Not there, let's created it
    if (!flow_dir) {
        flow_dir = new FlowDirector(port_id, errh);
        assert(flow_dir);
        _dev_flow_dir[port_id] = flow_dir;
    }

    // Create a Flow Director parser
    _parser = parser(errh);

    // Ship it back
    return flow_dir;
}

/**
 * Installs a set of string-based rules read from a file.
 * Allowed rule type is only a `create` rule.
 *
 * @param filename the file that contains the rules
 */
int
FlowDirector::add_rules_from_file(const String &filename)
{
    uint32_t rules_nb = 0;
    uint32_t installed_rules_nb = flow_rules_count();

    FILE *fp = NULL;
    fp = fopen(filename.c_str(), "r");
    if (fp == NULL) {
        return _errh->error(
            "Flow Director (port %u): Failed to open file '%s'",
            _port_id, filename.c_str());
    }

    char *rule = NULL;
    size_t len = 0;
    const char ignore_chars[] = "\n\t ";

    // Read file line-by-line (or rule-by-rule)
    while ((getline(&rule, &len, fp)) != -1) {
        // Skip empty lines or lines with only spaces/tabs
        if (!rule || (strlen(rule) == 0) ||
            (strchr(ignore_chars, rule[0]))) {
            _errh->warning("Flow Director (port %u): Invalid rule #%" PRIu32, _port_id, rules_nb++);
            continue;
        }

        // Filter out irrelevant DPDK commands
        if (!strstr(rule, "create")) {
            _errh->warning(
                "Flow Director (port %u): "
                "Rule #%" PRIu32 " does not contain create pattern", _port_id, rules_nb++
            );
            continue;
        }

        if (_verbose) {
            _errh->message("[NIC %u] About to install rule #%" PRIu32 ": %s",
                _port_id, rules_nb++, rule
            );
        }

        if (flow_rule_install(installed_rules_nb, rule) == SUCCESS) {
            installed_rules_nb++;
        }
    }

    // Close the file
    fclose(fp);

    // Free memory
    if (rule) {
        free(rule);
    }

    _errh->message(
        "Flow Director (port %u): %" PRIu32 "/%" PRIu32 " rules are installed",
        _port_id, flow_rules_count(), rules_nb
    );

    return SUCCESS;
}

/**
 * Translates a string-based rule into a flow rule
 * object and installs it in a NIC.
 *
 * @param rule_id a flow rule's ID
 * @param rule a flow rule as a string
 * @return a flow rule object
 */
int
FlowDirector::flow_rule_install(const uint32_t &rule_id, const char *rule)
{
    // Only active instances can configure a NIC
    if (!active()) {
        _errh->error(
            "Flow Director (port %u): Inactive instance cannot install rule #%4u",
            _port_id, rule_id
        );
        return ERROR;
    }


    // TODO: Fix DPDK to return proper status
    int res = flow_parser_parse(_parser, (char *) rule, _errh);
    if (res >= 0) {
        _rules_nb[_port_id]++;
        _next_rule_id[_port_id]++;
        return SUCCESS;
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

    _errh->error(
        "Flow Director (port %u): Failed to parse rule #%4u due to %s",
        _port_id, rule_id, error.c_str()
    );

    return ERROR;
}

/**
 * Returns a flow rule object of a specific port with a specific ID.
 *
 * @param rule_id a rule ID
 * @return a flow rule object
 */
struct port_flow *
FlowDirector::flow_rule_get(const uint32_t &rule_id)
{
    struct rte_port *port = get_port(_port_id);
    if (!port->flow_list) {
        return NULL;
    }

    for (struct port_flow *pf = port->flow_list; pf != NULL; pf = pf->next) {
        if (pf->id == rule_id) {
            return pf;
        }
    }

    return NULL;
}

/**
 * Removes a flow rule object from the NIC.
 *
 * @param rule_id a flow rule's ID
 * @return status
 */
int
FlowDirector::flow_rule_delete(const uint32_t &rule_id)
{
    // Only active instances can configure a NIC
    if (!active()) {
        return ERROR;
    }

    const uint32_t rules_to_delete[] = {rule_id};

    if (port_flow_destroy(_port_id, 1, rules_to_delete) == FLOWDIR_SUCCESS) {
        _rules_nb[_port_id]--;
        return SUCCESS;
    }

    return ERROR;
}

/**
 * Return the explicit rule counter for a particular NIC.
 *
 * @return the number of rules being installed
 */
uint32_t
FlowDirector::flow_rules_count()
{
    return _rules_nb[_port_id];
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
            _errh->message("Flow Director (port %u): No flows", _port_id);
        }
        return 0;
    }

    uint32_t rules_nb = 0;

    // Traverse the list of installed flow rules
    for (struct port_flow *pf = port->flow_list; pf != NULL; pf = pf->next) {
        rules_nb++;
    }

    // Consistency
    assert(_rules_nb[_port_id] == rules_nb);

    return rules_nb;
}

/**
 * Flushes all of the rules from a NIC associated with this
 * Flow Director instance.
 *
 * @return the number of rules being flushed
 */
uint32_t
FlowDirector::flow_rules_flush()
{
    // Only active instances can configure a NIC
    if (!active()) {
        if (verbose()) {
            _errh->message("Flow Director (port %u): Nothing to flush", _port_id);
        }
        return 0;
    }

    uint32_t rules_before_flush = flow_rules_count_explicit();
    if (rules_before_flush == 0) {
        return 0;
    }

    // Successful flush means zero rules left
    if (port_flow_flush(_port_id) == FLOWDIR_SUCCESS) {
        _rules_nb[_port_id] = 0;
        return rules_before_flush;
    }

    // Now, count again to verify what is left
    return flow_rules_count_explicit();
}

#endif /* RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0) */

CLICK_ENDDECLS
