/*
 * dpdkdevice.{cc,hh} -- library for interfacing with Intel's DPDK
 * Cyril Soldani, Tom Barbette
 *
 * Copyright (c) 2014-2016 University of Liege
 * Copyright (c) 2016 Cisco Meraki
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
#include <click/dpdkdevice.hh>
#include <rte_errno.h>
#if RTE_VERSION >= RTE_VERSION_NUM(17,02,0,0)
extern "C" {
#include <rte_pmd_ixgbe.h>
}
#endif

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
extern "C" {
#include <rte_flow.h>
}
#endif

CLICK_DECLS

/**
 * Flow Director Implementation.
 */
#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)

#define MASK_FROM_PREFIX(p) ~((1 << (32 - p)) - 1)

// Unifies TCP and UDP parsing
union L4_RULE {
    enum rte_flow_item_type type;
    struct rte_flow_item_udp udp;
    struct rte_flow_item_tcp tcp;
    struct rte_flow_item_any any;
} L4_RULE;
// DPDKDevice mode
String FlowDirector::FLOW_DIR_FLAG = "flow_dir";

// Supported flow director handlers (called from FromDPDKDevice)
String FlowDirector::FLOW_RULE_ADD   = "add_rule";
String FlowDirector::FLOW_RULE_DEL   = "del_rule";
String FlowDirector::FLOW_RULE_LIST  = "count_rules";
String FlowDirector::FLOW_RULE_FLUSH = "flush_rules";

// Rule structure constants
String FlowDirector::FLOW_RULE_PATTERN = "pattern";
String FlowDirector::FLOW_RULE_ACTION  = "action";
String FlowDirector::FLOW_RULE_IP4_PREFIX = "/";

// Supported patterns
const Vector<String> FlowDirector::FLOW_RULE_PATTERNS_VEC = [] {
    Vector<String> v;
    v.push_back("ip src");
    v.push_back("ip dst");
    v.push_back("tcp port src");
    v.push_back("tcp port dst");
    v.push_back("udp port src");
    v.push_back("udp port dst");
    return v;
}();

// Supported actions
const Vector<String> FlowDirector::FLOW_RULE_ACTIONS_VEC = [] {
    Vector<String> v;
    v.push_back("queue index");
    return v;
}();

// Static list of rules
const Vector<String> FlowDirector::FLOW_RULES_VEC = [] {
    Vector<String> v;
    v.push_back("pattern ip src 10.0.0.1/16 and ip dst 192.168.0.1/24 and tcp port dst 80 action queue index 0");
    v.push_back("pattern ip src 11.0.0.1/32 and ip dst 192.168.0.2 and udp port src 12678 action queue index 15");
    return v;
}();

// Global table of DPDK ports mapped to their Flow Director objects
HashTable<uint8_t, FlowDirector *> FlowDirector::_dev_flow_dir;

// A unique error handler
ErrorVeneer *FlowDirector::_errh;

FlowDirector::FlowDirector() :
        _port_id(-1), _active(false), _verbose(false)
{
    _errh = new ErrorVeneer(ErrorHandler::default_handler());
}

FlowDirector::FlowDirector(uint8_t port_id, ErrorHandler *errh) :
        _port_id(port_id), _active(false), _verbose(false)
{
    _errh = new ErrorVeneer(errh);

    if (_verbose) {
        _errh->message("Flow Director (port %u): Created", port_id);
    }
}

FlowDirector::~FlowDirector()
{
    if (_verbose) {
        _errh->message("Flow Director (port %u): Destroyed", _port_id);
    }
}

/**
 * Manages the Flow Director instances.
 *
 * @param port_id the ID of the NIC
 * @return a Flow Director object for this NIC
 */
FlowDirector *FlowDirector::get_flow_director(const uint8_t &port_id, ErrorHandler *errh)
{
    // Invalid port ID
    if (port_id >= rte_eth_dev_count()) {
        click_chatter("Flow Director (port %u): Denied to create instance for invalid port", port_id);
        return 0;
    }

    // Get the Flow Director of the desired port
    FlowDirector *flow_dir = _dev_flow_dir.get(port_id);

    // Not there, let's created it
    if (!flow_dir) {
        flow_dir = new FlowDirector(port_id, errh);
        _dev_flow_dir[port_id] = flow_dir;
    }

    // Ship it back
    return flow_dir;
}

/**
 * Installs a set of string-based rules read from a static vector.
 *
 * @param port_id the ID of the NIC
 * @param rules the vector that contains the rules
 * @return status
 */
bool FlowDirector::add_rules_static(const uint8_t &port_id, const Vector<String> rules)
{
    for (uint32_t i=0 ; i<rules.size() ; i++) {
        // Add flow handler
        FlowDirector::flow_rule_install(port_id, i, rules[i]);
    }

    _errh->message(
        "Flow Director (port %u): %u/%u rules are installed",
        port_id, _dev_flow_dir[port_id]->_rule_list.size(), rules.size()
    );

    return true;
}

/**
 * Installs a set of string-based rules read from a file.
 *
 * @param port_id the ID of the NIC
 * @param filename the file that contains the rules
 * @return status
 */
bool FlowDirector::add_rules_file(const uint8_t &port_id, const String &filename)
{
    // _errh->error("Flow Director (port %u): Add rules from file is not currently supported", port_id);

    // ifstream myfile(filename);
    // if (myfile.is_open()) {
    //     String line;
    //     while ( getline(myfile, line) ) {
    //         click_chatter();
    //     }
    //     myfile.close();
    // }

    return false;
}

/**
 * Translate a string-based rule into a flow rule object
 * and install it to the NIC.
 *
 * @param port_id the ID of the NIC
 * @param rule_id a flow rule's ID
 * @param rule a flow rule as a string
 * @return a flow rule object
 */
bool FlowDirector::flow_rule_install(const uint8_t &port_id, const uint32_t &rule_id, const String &rule)
{
    // Only active instances can configure a NIC
    if (!_dev_flow_dir[port_id]->get_active()) {
        return false;
    }

    //////////////////////////////////////////////////////////////////////
    // Flow Attributes: Only ingress rules are currently supported
    //////////////////////////////////////////////////////////////////////
    struct rte_flow_attr attr = {
        .group    = 0,
        .priority = 0,
        .ingress  = 1,
        .egress   = 0,
        .reserved = 0,
    };
    //////////////////////////////////////////////////////////////////////


    //////////////////////////////////////////////////////////////////////
    // Flow Patterns
    //////////////////////////////////////////////////////////////////////
    // L2 - Ethernet type is always IPv4
    struct rte_flow_item_eth flow_item_eth_type_ipv4 = {
        .dst = {
            .addr_bytes = { 0 }
        },
        .src = {
            .addr_bytes = { 0 }
        },
        .type = rte_cpu_to_be_16(ETHER_TYPE_IPv4)
    };

    struct rte_flow_item_eth flow_item_eth_mask_type_ipv4 = {
        .dst = {
            .addr_bytes = { 0 }
        },
        .src = {
            .addr_bytes = { 0 }
        },
        .type = rte_cpu_to_be_16(0xFFFF)
    };
    //////////////////////////////////////////////////////////////////////


    //////////////////////////////////////////////////////////////////////
    // Flow Actions
    //////////////////////////////////////////////////////////////////////
    // Only queue -> core dispatching is currently supported
    struct rte_flow_action_queue queue_conf;
    //////////////////////////////////////////////////////////////////////


    // Every rule must start with 'pattern'
    if (!rule.starts_with(FlowDirector::FLOW_RULE_PATTERN)) {
        flow_rule_usage(port_id, "Rule must begin with pattern keyword");
        return false;
    }

    // Useful indices
    int pattern_start = FlowDirector::FLOW_RULE_PATTERN.length() + 1;
    int action_pos = rule.find_left(FlowDirector::FLOW_RULE_ACTION, pattern_start);
    if (action_pos < 0) {
        flow_rule_usage(port_id, "Rule without action keyword is invalid");
        return false;
    }
    int action_start = action_pos + FlowDirector::FLOW_RULE_ACTION.length() + 1;
    int pattern_end = action_pos - 1;

    // Patterns and actions are separated
    String pattern_str = rule.substring(pattern_start, pattern_end - pattern_start + 1).trim_space();
    String action_str  = rule.substring(action_start).trim_space();

    // Split a pattern into multiple sub-patterns
    Vector<String> sub_patterns;
    int pattern_index = 0;
    while (pattern_index >= 0) {
        int until = pattern_str.find_left("and", pattern_index);
        if (until < 0) {
            // Keep this last part until the end
            sub_patterns.push_back(pattern_str.substring(pattern_index + 1).trim_space_left().trim_space());
            break;
        }
        // Keep this sub-pattern
        sub_patterns.push_back(pattern_str.substring(pattern_index, until - pattern_index - 1).trim_space_left().trim_space());

        // Move to the next, after 'and '
        pattern_index = until + 3;
    }

    if (sub_patterns.size() == 0) {
        flow_rule_usage(port_id, "Rule begins with pattern but has no patterns");
        return false;
    }

    if (_dev_flow_dir[port_id]->get_verbose()) {
        _errh->message("Flow Director (port %u): Parsing flow rule #%4u", port_id, rule_id);
    }

    // Fill the pattern's data
    bool valid_pattern = true;
    uint8_t  ip_proto = 0x00;
    uint8_t  ip_proto_mask = 0x00;
    uint8_t  ip_src[4];
    uint8_t  ip_dst[4];
    uint32_t ip_src_mask = rte_cpu_to_be_32(0xFFFFFFFF);
    uint32_t ip_dst_mask = rte_cpu_to_be_32(0xFFFFFFFF);
    uint16_t port_src = rte_cpu_to_be_16(0x0000);
    uint16_t port_dst = rte_cpu_to_be_16(0x0000);
    uint16_t port_src_mask = rte_cpu_to_be_16(0x0000);
    uint16_t port_dst_mask = rte_cpu_to_be_16(0x0000);
    String proto = "";
    for (String pat : sub_patterns) {
        // Valid patterns
        for (String p : FLOW_RULE_PATTERNS_VEC) {
            // This is a valid pattern
            if (pat.starts_with(p)) {
                String ip_str = pat.substring(p.length() + 1);

                // IPv4 address match
                if ((p == "ip src") || (p == "ip dst")) {
                    String ip_prefix_str;
                    String ip_rule_str;

                    // Check whether a prefix is given
                    int prefix_pos = ip_str.find_left(FlowDirector::FLOW_RULE_IP4_PREFIX, 0);
                    uint32_t ip_prefix = 0;

                    // There is a prefix after the IP
                    if (prefix_pos >= 0) {
                        int first_space_pos = ip_str.find_left(' ', prefix_pos);
                        ip_prefix_str = ip_str.substring(prefix_pos + 1);
                        ip_prefix = atoi(ip_prefix_str.c_str());
                        ip_rule_str = ip_str.substring(0, prefix_pos);
                    } else {
                        ip_rule_str = ip_str;
                    }

                    unsigned prev = 0;
                    unsigned curr = 0;
                    unsigned j = 0;
                    while(curr < ip_rule_str.length()) {
                        if ((ip_rule_str.at(curr) == '.') || (curr == (ip_rule_str.length()-1))) {
                            unsigned offset = (curr-prev > 0) ? (curr-prev) : 1;
                            String ip_byte = ip_rule_str.substring(prev, offset);

                            if (p == "ip src") {
                                ip_src[j] = atoi(ip_byte.c_str());
                            } else if (p == "ip dst") {
                                ip_dst[j] = atoi(ip_byte.c_str());
                            } else {
                                continue;
                            }

                            prev = curr + 1;
                            curr++;
                            j++;
                        } else {
                            curr++;
                        }
                    }

                    // Set the mask based on the prefix
                    if (ip_prefix > 0) {
                        (p == "ip src") ? ip_src_mask = rte_cpu_to_be_32(MASK_FROM_PREFIX(ip_prefix)):
                                          ip_dst_mask = rte_cpu_to_be_32(MASK_FROM_PREFIX(ip_prefix));
                    }

                    if (curr == (ip_rule_str.length()) && _dev_flow_dir[port_id]->get_verbose()) {
                        (p == "ip src") ?
                            _errh->message(
                                "\tL3 pattern: %s with IP: %u.%u.%u.%u and mask %s",
                                pat.c_str(), ip_src[0], ip_src[1], ip_src[2], ip_src[3],
                                IPAddress(ip_src_mask).unparse().c_str()
                            )
                            :
                            _errh->message(
                                "\tL3 pattern: %s with IP: %u.%u.%u.%u and mask %s",
                                pat.c_str(), ip_dst[0], ip_dst[1], ip_dst[2], ip_dst[3],
                                IPAddress(ip_dst_mask).unparse().c_str()
                            );
                    }

                    continue;
                }

                // TCP match
                if (pat.starts_with("tcp")) {
                    proto = "tcp";
                    ip_proto = 0x06;
                // UDP match
                } else if (pat.starts_with("udp")) {
                    proto = "udp";
                    ip_proto = 0x11;
                } else {
                    valid_pattern = false;
                    continue;
                }
                ip_proto_mask = 0xFF;

                // Get src or dst type
                String port_type = pat.substring(9, 3);
                // Get port number
                String port_str = pat.substring(p.length() + 1);

                if (port_type == "src") {
                    port_src = atoi(port_str.c_str());
                    port_src_mask = rte_cpu_to_be_16(0xFFFF);
                } else {
                    port_dst = atoi(port_str.c_str());
                    port_dst_mask = rte_cpu_to_be_16(0xFFFF);
                }

                if (_dev_flow_dir[port_id]->get_verbose()) {
                    _errh->message(
                        "\tL4 pattern: Proto %s (%u), Port type: %s, Port No: %s",
                        proto.c_str(), ip_proto, port_type.c_str(), port_str.c_str()
                    );
                }
            }
        }
    }

    if (!valid_pattern) {
        flow_rule_usage(port_id, String("Unsupported pattern " + pattern_str).c_str());
        return false;
    }

    // Compose the IP pattern
    struct rte_flow_item_ipv4 ip_rule = {
        .hdr = {
            .version_ihl     = 0x00,
            .type_of_service = 0x00,
            .total_length    = rte_cpu_to_be_16(0x0000),
            .packet_id       = rte_cpu_to_be_16(0x0000),
            .fragment_offset = rte_cpu_to_be_16(0x0000),
            .time_to_live    = 0x00,
            .next_proto_id   = ip_proto,
            .hdr_checksum    = rte_cpu_to_be_16(0x0000),
            .src_addr        = rte_cpu_to_be_32(IPv4(ip_src[0], ip_src[1], ip_src[2], ip_src[3])),
            .dst_addr        = rte_cpu_to_be_32(IPv4(ip_dst[0], ip_dst[1], ip_dst[2], ip_dst[3])),
        },
    };

    struct rte_flow_item_ipv4 ip_rule_mask = {
        .hdr = {
            .version_ihl     = 0x00,
            .type_of_service = 0x00,
            .total_length    = rte_cpu_to_be_16(0x0000),
            .packet_id       = rte_cpu_to_be_16(0x0000),
            .fragment_offset = rte_cpu_to_be_16(0x0000),
            .time_to_live    = 0x00,
            .next_proto_id   = ip_proto_mask,
            .hdr_checksum    = rte_cpu_to_be_16(0x0000),
            .src_addr        = ip_src_mask,
            .dst_addr        = ip_dst_mask,
        },
    };

    // Compose the transport layer pattern
    union L4_RULE l4_rule;
    union L4_RULE l4_rule_mask;
    void *l4_rule_spec = NULL;
    void *l4_rule_mask_spec = NULL;
    // TCP matching
    if (proto == "tcp") {
        l4_rule.type = RTE_FLOW_ITEM_TYPE_TCP;

        l4_rule.tcp.hdr.src_port = rte_cpu_to_be_16(port_src);
        l4_rule.tcp.hdr.dst_port = rte_cpu_to_be_16(port_dst);
        l4_rule_spec = (void *) &l4_rule.tcp;

        l4_rule_mask.tcp.hdr.src_port = rte_cpu_to_be_16(port_src_mask);
        l4_rule_mask.tcp.hdr.dst_port = rte_cpu_to_be_16(port_dst_mask);
        l4_rule_mask_spec = (void *) &l4_rule_mask.tcp;
    // UDP matching
    } else if (proto == "udp") {
        l4_rule.type = RTE_FLOW_ITEM_TYPE_UDP;

        l4_rule.udp.hdr.src_port = rte_cpu_to_be_16(port_src);
        l4_rule.udp.hdr.dst_port = rte_cpu_to_be_16(port_dst);
        l4_rule_spec = (struct rte_flow_item_udp *) &l4_rule.udp;

        l4_rule_mask.udp.hdr.src_port = rte_cpu_to_be_16(port_src_mask);
        l4_rule_mask.udp.hdr.dst_port = rte_cpu_to_be_16(port_dst_mask);
        l4_rule_mask_spec = (struct rte_flow_item_udp *) &l4_rule_mask.udp;
    // No L4 matching
    } else {
        l4_rule.type = RTE_FLOW_ITEM_TYPE_ANY;
        l4_rule.any.num = rte_cpu_to_be_32(1);
        l4_rule_spec = (void *) &l4_rule.any;
        l4_rule_mask.any.num = rte_cpu_to_be_32(0xFFFFFFFF);
        l4_rule_mask_spec = (void *) &l4_rule_mask.any;
    }

    // struct rte_flow_item_udp udp_rule;
    // udp_rule.hdr.src_port = rte_cpu_to_be_16(port_src);

    // struct rte_flow_item_udp udp_rule_mask;
    // udp_rule.hdr.src_port = rte_cpu_to_be_16(port_src_mask);

    // Compose the pattern
    struct rte_flow_item patterns[] = {
        {
            .type = RTE_FLOW_ITEM_TYPE_ETH,
            .spec = (void *) &flow_item_eth_type_ipv4,
            .last = NULL,
            .mask = (void *) &flow_item_eth_mask_type_ipv4,
        },
        {
            .type = RTE_FLOW_ITEM_TYPE_IPV4,
            .spec = &ip_rule,
            .last = NULL,
            .mask = &ip_rule_mask,
        },
        // TODO: When I assign my own void* it does not work. Works with direct assignment of structs (see udp_rule[_mask] above)
        // {
        //     .type = l4_rule.type,
        //     .spec = l4_rule_spec,
        //     .last = NULL,
        //     .mask = l4_rule_mask_spec,
        // },
        {
            .type = RTE_FLOW_ITEM_TYPE_END,
            .spec = NULL,
            .last = NULL,
            .mask = NULL,
        }
    };

    // Compose a DPDK action
    bool valid_action = false;
    for (String act : FLOW_RULE_ACTIONS_VEC) {
        if (action_str.starts_with(act) > 0) {
            String act_str = action_str.substring(act.length() + 1);
            uint16_t queue_index = atoi(act_str.c_str());

            // TODO: Ensure that there are enough queues
            // if (queue_index >= rx_queues_nb) {
            //     _errh->error("Flow Director (port %u): Not enough queues for action: %s", action_str.c_str());
            //     return false;
            // }

            queue_conf.index = queue_index;
            valid_action = true;

            if (_dev_flow_dir[port_id]->get_verbose()) {
                _errh->message("\tAction with queue index: %d", queue_index);
            }
        }
    }

    if (!valid_action) {
        flow_rule_usage(port_id, String("Unsupported action " + action_str).c_str());
        return false;
    }

    struct rte_flow_action actions[] =
    {
        {
            .type = RTE_FLOW_ACTION_TYPE_QUEUE,
            .conf = &queue_conf
        },
        {
            .type = RTE_FLOW_ACTION_TYPE_END,
            .conf = NULL
        }
    };

    // Validate the rule
    if (!flow_rule_validate(port_id, rule_id, &attr, patterns, actions)) {
        return false;
    }

    // Add the rule to the NIC and to our memory
    return flow_rule_add(port_id, rule_id, &attr, patterns, actions);
}

/**
 * Reports whether a flow rule would be accepted by the underlying
 * device in its current state.
 *
 * @param port_id  the ID of the NIC
 * @param rule_id  a flow rule's ID
 * @param attr     a flow rule's attributes
 * @param patterns a flow rule's patterns
 * @param actions  a flow rule's actions
 * @return status
 */
bool FlowDirector::flow_rule_validate(
        const uint8_t                &port_id,
        const uint32_t               &rule_id,
        const struct rte_flow_attr   *attr,
        const struct rte_flow_item   *patterns,
        const struct rte_flow_action *actions
)
{
    struct rte_flow_error error;

    /* Poisoning to make sure PMDs update it in case of error. */
    memset(&error, 0x11, sizeof(error));

    int ret = rte_flow_validate(port_id, attr, patterns, actions, &error);
    if (ret < 0) {
        flow_rule_complain(port_id, &error);
        return false;
    }

    _errh->message("Flow Director (port %u): Flow rule #%4u validated", port_id, rule_id);

    return true;
}

/**
 * Adds a flow rule object to the NIC and to our memory.
 *
 * @param port_id  the ID of the NIC
 * @param rule_id  a flow rule's ID
 * @param attr     a flow rule's attributes
 * @param patterns a flow rule's patterns
 * @param actions  a flow rule's actions
 * @return status
 */
bool FlowDirector::flow_rule_add(
        const uint8_t                &port_id,
        const uint32_t               &rule_id,
        const struct rte_flow_attr   *attr,
        const struct rte_flow_item   *patterns,
        const struct rte_flow_action *actions
)
{
    struct rte_flow  *flow = NULL;
    struct rte_flow_error error;

    /* Poisoning to make sure PMDs update it in case of error. */
    memset(&error, 0x22, sizeof(error));

    // Create a DPDK flow
    flow = rte_flow_create(port_id, attr, patterns, actions, &error);
    if (flow == NULL) {
        flow_rule_complain(port_id, &error);
        return false;
    }

    // Create the rule out of this flow
    struct port_flow *flow_rule = new struct port_flow(
        rule_id,
        (struct rte_flow *)        flow,
        (struct rte_flow_attr *)   attr,
        (struct rte_flow_item *)   patterns,
        (struct rte_flow_action *) actions
    );

    // Add it to the list
    _dev_flow_dir[port_id]->_rule_list.push_back(flow_rule);

    _errh->message("Flow Director (port %u): Flow rule #%4u created", port_id, rule_id);

    return true;
}

/**
 * Returns a flow rule object of a specific port with a specific ID.
 *
 * @param port_id the device ID
 * @param rule_id a rule ID
 * @return a flow rule object
 */
struct FlowDirector::port_flow *FlowDirector::flow_rule_get(const uint8_t &port_id, const uint32_t &rule_id)
{
    // Get the list of rules of this port
    Vector<struct port_flow *> port_rules = _dev_flow_dir[port_id]->_rule_list;
    if (!port_rules.empty()) {
        return 0;
    }

    for (unsigned i=0 ; i<port_rules.size() ; i++) {
        if (rule_id == port_rules[i]->rule_id) {
            return port_rules[i];
        }
    }

    return 0;
}

/**
 * Removes a flow rule object from the NIC and from our memory.
 *
 * @param port_id the ID of the NIC
 * @param rule_id a flow rule's ID
 * @return status
 */
bool FlowDirector::flow_rule_delete(const uint8_t &port_id, const uint32_t &rule_id)
{
    // Only active instances can configure a NIC
    if (!_dev_flow_dir[port_id]->get_active()) {
        return false;
    }

    struct port_flow *flow_rule = FlowDirector::flow_rule_get(port_id, rule_id);
    if (!flow_rule) {
        _errh->error("Flow Director (port %u): Flow rule #%4u not found", port_id, rule_id);
        return false;
    }

    struct rte_flow_error error;
    memset(&error, 0x33, sizeof(error));

    if (rte_flow_destroy(port_id, flow_rule->flow, &error) < 0) {
        flow_rule_complain(port_id, &error);
        return false;
    }
    _errh->message("Flow Director (port %u): Flow rule #%4u destroyed\n", port_id, flow_rule->rule_id);

    // Delete also from the vector
    // TODO: Check
    delete flow_rule;
    // for (HashTable<uint8_t, Vector<struct port_flow *>>::const_iterator it = _rule_list.begin();
    //         it != _rule_list.end(); ++it) {
    //     if (it.key() != port_id) {
    //         continue;
    //     }

    //     // Port found
    //     if (it.value()->rule_id == rule_id) {
    //         delete it.value();
    //         break;
    //     }
    // }

    return true;
}

/**
 * Flushes all the rules from the NIC.
 *
 * @param port_id the ID of the NIC
 * @return the number of rules being flushed
 */
uint32_t FlowDirector::flow_rules_flush(const uint8_t &port_id)
{
    // Only active instances can configure a NIC
    if (!_dev_flow_dir[port_id]->get_active()) {
        if (_dev_flow_dir[port_id]->get_verbose()) {
            _errh->message("Flow Director (port %u): Nothing to flush", port_id);
        }
        return 0;
    }

    // Get the list of rules of this port
    Vector<struct port_flow *> port_rules = _dev_flow_dir[port_id]->_rule_list;
    if (port_rules.empty()) {
        if (_dev_flow_dir[port_id]->get_verbose()) {
            _errh->message("Flow Director (port %u): Nothing to flush", port_id);
        }
        return 0;
    }

    struct rte_flow_error error;
    memset(&error, 0x44, sizeof(error));

    // First remove the rules from the NIC
    if (rte_flow_flush(port_id, &error)) {
        return flow_rule_complain(port_id, &error);
    }

    if (_dev_flow_dir[port_id]->get_verbose()) {
        _errh->message("Flow Director (port %u): NIC is flushed", port_id);
    }

    // And then clean up our memory
    return memory_clean(port_id);
}

/**
 * Clean up the rules of a particular NIC.
 *
 * @param port_id the ID of the NIC
 * @return the number of rules being flushed
 */
uint32_t FlowDirector::memory_clean(const uint8_t &port_id)
{
    // Get the list of rules of this port
    Vector<struct port_flow *> port_rules = _dev_flow_dir[port_id]->_rule_list;
    if (port_rules.empty()) {
        if (_dev_flow_dir[port_id]->get_verbose()) {
            _errh->message("Flow Director (port %u): Nothing to clean", port_id);
        }
        return 0;
    }

    // And then clean up our memory
    uint32_t rules_flushed = 0;
    for (unsigned i=0; i<port_rules.size(); i++) {
        if (port_rules[i] != NULL) {
            delete (port_rules[i]);
            rules_flushed++;
        }
    }
    port_rules.clear();

    if (_dev_flow_dir[port_id]->get_verbose()) {
        _errh->message("Flow Director (port %u): Flushed %u rules from memory", port_id, rules_flushed);
    }

    return rules_flushed;
}

/**
 * Reports the correct usage of a Flow Director
 * rule along with a message.
 *
 * @param port_id the ID of the NIC
 * @param message the message to be printed
 */
void FlowDirector::flow_rule_usage(const uint8_t &port_id, const char *message)
{
    _errh->error("Flow Director (port %u): %s", port_id, message);
    _errh->error("Flow Director (port %u): Usage: pattern [p1] and .. and [p2] action queue index [queue no]", port_id);
}

/**
 * Count all the rules installed to the NIC.
 *
 * @param port_id the ID of the NIC
 * @return the number of rules being installed
 */
uint32_t FlowDirector::flow_rules_count(const uint8_t &port_id)
{
    // Only active instances might have some rules
    if (!_dev_flow_dir[port_id]->get_active()) {
        return 0;
    }

    // Get the list of rules of this port
    Vector<struct port_flow *> port_rules = _dev_flow_dir[port_id]->_rule_list;

    // The size of the list is the number of current flow rules
    return (uint32_t) port_rules.size();
}

/**
 * Print a message out of a flow error.
 * (Copied from DPDK).
 *
 * @param port_id the ID of the NIC
 * @param error an instance of DPDK's error structure
 */
int FlowDirector::flow_rule_complain(const uint8_t &port_id, struct rte_flow_error *error)
{
    static const char *const errstrlist[] = {
        [RTE_FLOW_ERROR_TYPE_NONE] = "no error",
        [RTE_FLOW_ERROR_TYPE_UNSPECIFIED] = "cause unspecified",
        [RTE_FLOW_ERROR_TYPE_HANDLE] = "flow rule (handle)",
        [RTE_FLOW_ERROR_TYPE_ATTR_GROUP] = "group field",
        [RTE_FLOW_ERROR_TYPE_ATTR_PRIORITY] = "priority field",
        [RTE_FLOW_ERROR_TYPE_ATTR_INGRESS] = "ingress field",
        [RTE_FLOW_ERROR_TYPE_ATTR_EGRESS] = "egress field",
        [RTE_FLOW_ERROR_TYPE_ATTR] = "attributes structure",
        [RTE_FLOW_ERROR_TYPE_ITEM_NUM] = "pattern length",
        [RTE_FLOW_ERROR_TYPE_ITEM] = "specific pattern item",
        [RTE_FLOW_ERROR_TYPE_ACTION_NUM] = "number of actions",
        [RTE_FLOW_ERROR_TYPE_ACTION] = "specific action",
    };
    const char *errstr;
    char buf[32];
    int err = rte_errno;

    if ((unsigned int)error->type >= RTE_DIM(errstrlist) ||
        !errstrlist[error->type]) {
        errstr = "unknown type";
    }
    else {
        errstr = errstrlist[error->type];
    }

    _errh->error("Flow Director (port %u): Caught error type %d (%s): %s%s\n",
        port_id,
        error->type,
        errstr,
        error->cause ? (snprintf(buf, sizeof(buf), "cause: %p, ",
        error->cause), buf) : "",
        error->message ? error->message : "(no stated reason)"
    );

    return -err;
}

#endif

/* End of Flow Director */

/**
 * Called by the constructor of DPDKDevice.
 * Flow Director must be strictly invoked once for each port.
 *
 * @param port_id the ID of the device where Flow Director is invoked
 */
void DPDKDevice::initialize_flow_director(const uint8_t &port_id, ErrorHandler *errh)
{
    FlowDirector *flow_dir = FlowDirector::get_flow_director(port_id, errh);
    if (!flow_dir) {
        return;
    }

    // Already initialized
    if (flow_dir->get_port_id() >= 0) {
        return;
    }

    flow_dir->set_port_id(port_id);
    flow_dir->set_active(false);

    if (FlowDirector::_dev_flow_dir[port_id]->get_verbose()) {
        click_chatter("Flow Director (port %u): Port is set", port_id);
    }
}

/* Wraps rte_eth_dev_socket_id(), which may return -1 for valid ports when NUMA
 * is not well supported. This function will return 0 instead in that case. */
int DPDKDevice::get_port_numa_node(uint8_t port_id)
{
    if (port_id >= rte_eth_dev_count())
        return -1;
    int numa_node = rte_eth_dev_socket_id(port_id);
    return (numa_node == -1) ? 0 : numa_node;
}

unsigned int DPDKDevice::get_nb_txdesc()
{
    return info.n_tx_descs;
}

/**
 * This function is called by DPDK when Click run as a secondary process. It
 *     checks that the prefix match with the given config prefix and add it if it does so.
 */
#if RTE_VERSION >= RTE_VERSION_NUM(16,07,0,0)
void add_pool(struct rte_mempool * rte, void *arg){
#else
void add_pool(const struct rte_mempool * rte, void *arg){
#endif
    int* i = (int*)arg;
    if (strncmp(DPDKDevice::MEMPOOL_PREFIX.c_str(), const_cast<struct rte_mempool *>(rte)->name, DPDKDevice::MEMPOOL_PREFIX.length()) != 0)
        return;
    DPDKDevice::_pktmbuf_pools[*i] = const_cast<struct rte_mempool *>(rte);
    click_chatter("Found DPDK primary pool #%d %s",*i, DPDKDevice::_pktmbuf_pools[*i]->name);
    (*i)++;
}

int core_to_numa_node(unsigned lcore_id) {
       int numa_node = rte_lcore_to_socket_id(lcore_id);
       return (numa_node < 0) ? 0 : numa_node;
}

int DPDKDevice::alloc_pktmbufs()
{
    /* Count NUMA sockets for each device and each node, we do not want to
     * allocate a unused pool
     */
    int max_socket = -1;
    for (HashTable<uint8_t, DPDKDevice>::const_iterator it = _devs.begin();
         it != _devs.end(); ++it) {
        int numa_node = DPDKDevice::get_port_numa_node(it.key());
        if (numa_node > max_socket)
            max_socket = numa_node;
    }
    int i;
    RTE_LCORE_FOREACH(i) {
        int numa_node = core_to_numa_node(i);
        if (numa_node > max_socket)
            max_socket = numa_node;
    }


    if (max_socket == -1)
        return -1;

    _nr_pktmbuf_pools = max_socket + 1;

    // Allocate pktmbuf_pool array
    typedef struct rte_mempool *rte_mempool_p;
    _pktmbuf_pools = new rte_mempool_p[_nr_pktmbuf_pools];
    if (!_pktmbuf_pools)
        return -1;
    memset(_pktmbuf_pools, 0, _nr_pktmbuf_pools * sizeof(rte_mempool_p));

    if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
        // Create a pktmbuf pool for each active socket
        for (int i = 0; i < _nr_pktmbuf_pools; i++) {
                if (!_pktmbuf_pools[i]) {
                        String mempool_name = DPDKDevice::MEMPOOL_PREFIX + String(i);
                        const char* name = mempool_name.c_str();
                        _pktmbuf_pools[i] =
#if RTE_VERSION >= RTE_VERSION_NUM(2,2,0,0)
                        rte_pktmbuf_pool_create(name, NB_MBUF,
                                                MBUF_CACHE_SIZE, 0, MBUF_DATA_SIZE, i);
#else
                        rte_mempool_create(
                                        name, NB_MBUF, MBUF_SIZE, MBUF_CACHE_SIZE,
                                        sizeof (struct rte_pktmbuf_pool_private),
                                        rte_pktmbuf_pool_init, NULL, rte_pktmbuf_init, NULL,
                                        i, 0);
#endif

                        if (!_pktmbuf_pools[i])
                                return rte_errno;
                }
        }
    } else {
        int i = 0;
        rte_mempool_walk(add_pool,(void*)&i);
        if (i == 0) {
            click_chatter("Could not get pools from the primary DPDK process");
            return -1;
        }
    }

    return 0;
}

struct rte_mempool *DPDKDevice::get_mpool(unsigned int socket_id) {
    return _pktmbuf_pools[socket_id];
}

int DPDKDevice::set_mode(String mode, int num_pools, Vector<int> vf_vlan, ErrorHandler *errh) {
    mode = mode.lower();

    enum rte_eth_rx_mq_mode m;

    if (mode == "") {
        return 0;
    } else if ((mode == "none") || (mode == "flow_dir")) {
            m = ETH_MQ_RX_NONE;
    } else if (mode == "rss") {
            m = ETH_MQ_RX_RSS;
    } else if (mode == "vmdq") {
            m = ETH_MQ_RX_VMDQ_ONLY;
    } else if (mode == "vmdq_rss") {
            m = ETH_MQ_RX_VMDQ_RSS;
    } else if (mode == "vmdq_dcb") {
            m = ETH_MQ_RX_VMDQ_DCB;
    } else if (mode == "vmdq_dcb_rss") {
            m = ETH_MQ_RX_VMDQ_DCB_RSS;
    } else {
        return errh->error("Unknown mode %s",mode.c_str());
    }

    if (m != info.mq_mode && info.mq_mode != -1) {
        return errh->error("Device can only have one mode.");
    }

    if (m & ETH_MQ_RX_VMDQ_FLAG) {
        if (num_pools != info.num_pools && info.num_pools != 0) {
            return errh->error("Number of VF pools must be consistent for the same device");
        }
        if (vf_vlan.size() > 0) {
            if (info.vf_vlan.size() > 0)
                return errh->error("VF_VLAN can only be setted once per device");
            if (vf_vlan.size() != num_pools) {
                return errh->error("Number of VF_VLAN must be equal to the number of pools");
            }
            info.vf_vlan = vf_vlan;
        }

        if (num_pools) {
            info.num_pools = num_pools;
        }

    }

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
    if (mode == FlowDirector::FLOW_DIR_FLAG) {
        FlowDirector *flow_dir = FlowDirector::get_flow_director(port_id, errh);
        click_chatter("Flow Director (port %u): Active", port_id);
        flow_dir->set_active(true);
    }
#endif

    info.mq_mode = m;
    info.mq_mode_str = mode;

    return 0;
}

static struct ether_addr pool_addr_template = {
        .addr_bytes = {0x52, 0x54, 0x00, 0x00, 0x00, 0x00}
};

struct ether_addr DPDKDevice::gen_mac( int a, int b) {
    struct ether_addr mac;
     if (info.mac != EtherAddress()) {
         memcpy(&mac,info.mac.data(),sizeof(struct ether_addr));
     } else
         mac = pool_addr_template;
    mac.addr_bytes[4] = a;
    mac.addr_bytes[5] = b;
    return mac;
}

int DPDKDevice::initialize_device(ErrorHandler *errh)
{
    struct rte_eth_conf dev_conf;
    struct rte_eth_dev_info dev_info;
    memset(&dev_conf, 0, sizeof dev_conf);

    rte_eth_dev_info_get(port_id, &dev_info);

    info.mq_mode = (info.mq_mode == -1? ETH_MQ_RX_RSS : info.mq_mode);
    dev_conf.rxmode.mq_mode = info.mq_mode;
    dev_conf.rxmode.hw_vlan_filter = 0;

    if (info.mq_mode & ETH_MQ_RX_VMDQ_FLAG) {

        if (info.num_pools > dev_info.max_vmdq_pools) {
            return errh->error("The number of VF Pools exceeds the hardware limit of %d",dev_info.max_vmdq_pools);
        }

        if (info.rx_queues.size() % info.num_pools != 0) {
            info.rx_queues.resize(((info.rx_queues.size() / info.num_pools) + 1) * info.num_pools);
        }
        dev_conf.rx_adv_conf.vmdq_rx_conf.nb_queue_pools =  (enum rte_eth_nb_pools)info.num_pools;
        dev_conf.rx_adv_conf.vmdq_rx_conf.enable_default_pool = 0;
        dev_conf.rx_adv_conf.vmdq_rx_conf.default_pool = 0;
        if (info.vf_vlan.size() > 0) {
            dev_conf.rx_adv_conf.vmdq_rx_conf.rx_mode = 0;
            dev_conf.rx_adv_conf.vmdq_rx_conf.nb_pool_maps = info.num_pools;
            for (int i = 0; i < dev_conf.rx_adv_conf.vmdq_rx_conf.nb_pool_maps; i++) {
                dev_conf.rx_adv_conf.vmdq_rx_conf.pool_map[i].vlan_id = info.vf_vlan[i];
                dev_conf.rx_adv_conf.vmdq_rx_conf.pool_map[i].pools = (1UL << (i % info.num_pools));
            }
        } else {
            dev_conf.rx_adv_conf.vmdq_rx_conf.rx_mode = ETH_VMDQ_ACCEPT_UNTAG;
            dev_conf.rx_adv_conf.vmdq_rx_conf.nb_pool_maps = 0;
        }

    }
    if (info.mq_mode & ETH_MQ_RX_RSS_FLAG) {
        dev_conf.rx_adv_conf.rss_conf.rss_key = NULL;
        dev_conf.rx_adv_conf.rss_conf.rss_hf = ETH_RSS_IP | ETH_RSS_UDP | ETH_RSS_TCP;
    }

    //We must open at least one queue per direction
    if (info.rx_queues.size() == 0) {
        info.rx_queues.resize(1);
        info.n_rx_descs = 64;
    }
    if (info.tx_queues.size() == 0) {
        info.tx_queues.resize(1);
        info.n_tx_descs = 64;
    }

    if (rte_eth_dev_configure(port_id, info.rx_queues.size(), info.tx_queues.size(),
                              &dev_conf) < 0)
        return errh->error(
            "Cannot initialize DPDK port %u with %u RX and %u TX queues",
            port_id, info.rx_queues.size(), info.tx_queues.size());

    rte_eth_dev_info_get(port_id, &dev_info);

#if RTE_VERSION >= RTE_VERSION_NUM(16,07,0,0)
    if (dev_info.nb_rx_queues != info.rx_queues.size()) {
        return errh->error("Device only initialized %d RX queues instead of %d. "
                "Please check configuration.", dev_info.nb_rx_queues,
                info.rx_queues.size());
    }
    if (dev_info.nb_tx_queues != info.tx_queues.size()) {
        return errh->error("Device only initialized %d TX queues instead of %d. "
                "Please check configuration.", dev_info.nb_tx_queues,
                info.tx_queues.size());
    }
#endif

    struct rte_eth_rxconf rx_conf;
#if RTE_VERSION >= RTE_VERSION_NUM(2,0,0,0)
    memcpy(&rx_conf, &dev_info.default_rxconf, sizeof rx_conf);
#else
    bzero(&rx_conf,sizeof rx_conf);
#endif
    rx_conf.rx_thresh.pthresh = RX_PTHRESH;
    rx_conf.rx_thresh.hthresh = RX_HTHRESH;
    rx_conf.rx_thresh.wthresh = RX_WTHRESH;

    struct rte_eth_txconf tx_conf;
#if RTE_VERSION >= RTE_VERSION_NUM(2,0,0,0)
    memcpy(&tx_conf, &dev_info.default_txconf, sizeof tx_conf);
#else
    bzero(&tx_conf,sizeof tx_conf);
#endif
    tx_conf.tx_thresh.pthresh = TX_PTHRESH;
    tx_conf.tx_thresh.hthresh = TX_HTHRESH;
    tx_conf.tx_thresh.wthresh = TX_WTHRESH;
    tx_conf.txq_flags |= ETH_TXQ_FLAGS_NOMULTSEGS | ETH_TXQ_FLAGS_NOOFFLOADS;

    int numa_node = DPDKDevice::get_port_numa_node(port_id);
    for (unsigned i = 0; i < info.rx_queues.size(); ++i) {
        if (rte_eth_rx_queue_setup(
                port_id, i, info.n_rx_descs, numa_node, &rx_conf,
                _pktmbuf_pools[numa_node]) != 0)
            return errh->error(
                "Cannot initialize RX queue %u of port %u on node %u : %s",
                i, port_id, numa_node, rte_strerror(rte_errno));
    }

    for (unsigned i = 0; i < info.tx_queues.size(); ++i)
        if (rte_eth_tx_queue_setup(port_id, i, info.n_tx_descs, numa_node,
                                   &tx_conf) != 0)
            return errh->error(
                "Cannot initialize TX queue %u of port %u on node %u",
                i, port_id, numa_node);

    int err = rte_eth_dev_start(port_id);
    if (err < 0)
        return errh->error(
            "Cannot start DPDK port %u: error %d", port_id, err);

    if (info.promisc)
        rte_eth_promiscuous_enable(port_id);

    if (info.mac != EtherAddress()) {
        struct ether_addr addr;
        memcpy(&addr,info.mac.data(),sizeof(struct ether_addr));
        rte_eth_dev_default_mac_addr_set(port_id, &addr);
    }

    if (info.mq_mode & ETH_MQ_RX_VMDQ_FLAG) {
        /*
         * Set mac for each pool and parameters
         */
        for (unsigned q = 0; q < info.num_pools; q++) {
                struct ether_addr mac;
                mac = gen_mac(port_id, q);
                printf("Port %u vmdq pool %u set mac %02x:%02x:%02x:%02x:%02x:%02x\n",
                        port_id, q,
                        mac.addr_bytes[0], mac.addr_bytes[1],
                        mac.addr_bytes[2], mac.addr_bytes[3],
                        mac.addr_bytes[4], mac.addr_bytes[5]);
                int retval = rte_eth_dev_mac_addr_add(port_id, &mac,
                                q);
                if (retval) {
                        printf("mac addr add failed at pool %d\n", q);
                        return retval;
                }
        }
    }

    return 0;
}

void DPDKDevice::set_mac(EtherAddress mac) {
    assert(!_is_initialized);
    info.mac = mac;
}

/**
 * Set v[id] to true in vector v, expanding it if necessary. If id is 0,
 * the first available slot will be taken.
 * If v[id] is already true, this function return false. True if it is a
 *   new slot or if the existing slot was false.
 */
bool set_slot(Vector<bool> &v, unsigned &id) {
    if (id <= 0) {
        int i;
        for (i = 0; i < v.size(); i ++) {
            if (!v[i]) break;
        }
        id = i;
        if (id >= v.size())
            v.resize(id + 1, false);
    }
    if (id >= v.size()) {
        v.resize(id + 1,false);
    }
    if (v[id])
        return false;
    v[id] = true;
    return true;
}

int DPDKDevice::add_queue(DPDKDevice::Dir dir,
                           unsigned &queue_id, bool promisc, unsigned n_desc,
                           ErrorHandler *errh)
{
    if (_is_initialized) {
        return errh->error(
            "Trying to configure DPDK device after initialization");
    }

    if (dir == RX) {
        if (info.rx_queues.size() > 0 && promisc != info.promisc)
            return errh->error(
                "Some elements disagree on whether or not device %u should"
                " be in promiscuous mode", port_id);
        info.promisc |= promisc;
        if (n_desc > 0) {
            if (n_desc != info.n_rx_descs && info.rx_queues.size() > 0)
                return errh->error(
                        "Some elements disagree on the number of RX descriptors "
                        "for device %u", port_id);
            info.n_rx_descs = n_desc;
        }
        if (!set_slot(info.rx_queues, queue_id))
            return errh->error(
                        "Some elements are assigned to the same RX queue "
                        "for device %u", port_id);
    } else {
        if (n_desc > 0) {
            if (n_desc != info.n_tx_descs && info.tx_queues.size() > 0)
                return errh->error(
                        "Some elements disagree on the number of TX descriptors "
                        "for device %u", port_id);
            info.n_tx_descs = n_desc;
        }
        if (!set_slot(info.tx_queues,queue_id))
            return errh->error(
                        "Some elements are assigned to the same TX queue "
                        "for device %u", port_id);
    }

    return 0;
}

int DPDKDevice::add_rx_queue(unsigned &queue_id, bool promisc,
                              unsigned n_desc, ErrorHandler *errh)
{
    return add_queue(DPDKDevice::RX, queue_id, promisc, n_desc, errh);
}

int DPDKDevice::add_tx_queue(unsigned &queue_id, unsigned n_desc,
                              ErrorHandler *errh)
{
    return add_queue(DPDKDevice::TX, queue_id, false, n_desc, errh);
}

int DPDKDevice::initialize(const String &mode, ErrorHandler *errh)
{
    int ret;

    if (_is_initialized)
        return 0;

    pool_addr_template.addr_bytes[2] = click_random();
    pool_addr_template.addr_bytes[3] = click_random();

    if (!dpdk_enabled)
        return errh->error( "Supply the --dpdk argument to use DPDK.");

    click_chatter("Initializing DPDK");
#if RTE_VERSION < RTE_VERSION_NUM(2,0,0,0)
    if (rte_eal_pci_probe())
        return errh->error("Cannot probe the PCI bus");
#endif

    const unsigned n_ports = rte_eth_dev_count();
    if (n_ports == 0)
        return errh->error("No DPDK-enabled ethernet port found");

    for (HashTable<uint8_t, DPDKDevice>::const_iterator it = _devs.begin();
         it != _devs.end(); ++it)
        if (it.key() >= n_ports)
            return errh->error("Cannot find DPDK port %u", it.key());

    if ((ret = alloc_pktmbufs()) != 0)
        return errh->error("Could not allocate packet MBuf pools, error %d : %s",ret,rte_strerror(ret));

    if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
        for (HashTable<uint8_t, DPDKDevice>::iterator it = _devs.begin();
            it != _devs.end(); ++it) {
            int ret = it.value().initialize_device(errh);
            if (ret < 0)
                return ret;
        }
    }

    _is_initialized = true;

    // Configure Flow Director
#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
    for (HashTable<uint8_t, FlowDirector *>::iterator it = FlowDirector::_dev_flow_dir.begin();
            it != FlowDirector::_dev_flow_dir.end(); ++it) {

        // Only if the device is registered and has the correct mode
        if (it.value() && (mode == FlowDirector::FLOW_DIR_FLAG)) {
            DPDKDevice::configure_nic(it.key());
        }
    }
#endif

    return 0;
}

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
void DPDKDevice::configure_nic(const uint8_t &port_id)
{
    if (_is_initialized) {
        // Invoke Flow Director only if active
        if (FlowDirector::_dev_flow_dir[port_id]->get_active()) {
            FlowDirector::add_rules_static(port_id, FlowDirector::FLOW_RULES_VEC);
        }
    }
}
#endif

void DPDKDevice::free_pkt(unsigned char *, size_t, void *pktmbuf)
{
    rte_pktmbuf_free((struct rte_mbuf *) pktmbuf);
}

void DPDKDevice::cleanup(ErrorHandler *errh)
{
#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
    errh->message("\n");

    for (HashTable<uint8_t, FlowDirector *>::const_iterator it = FlowDirector::_dev_flow_dir.begin();
            it != FlowDirector::_dev_flow_dir.end(); ++it) {
        if (it == NULL) {
            continue;
        }

        // Flush
        uint32_t rules_flushed = FlowDirector::flow_rules_flush(it.key());

        // Delete this instance
        delete it.value();

        // Report
        if (rules_flushed > 0) {
            errh->message("Flow Director (port %u): Flushed %d rules from the NIC", it.key(), rules_flushed);
        }
    }

    // Clean up the table
    FlowDirector::_dev_flow_dir.clear();
#endif
}

bool
DPDKDeviceArg::parse(const String &str, DPDKDevice* &result, const ArgContext &ctx)
{
    uint8_t port_id;

    if (!IntArg().parse(str, port_id)) {
       //Try parsing a ffff:ff:ff.f format. Code adapted from EtherAddressArg::parse
        unsigned data[4];
        int d = 0, p = 0;
        const char *s, *end = str.end();

        for (s = str.begin(); s != end; ++s) {
           int digit;
           if (*s >= '0' && *s <= '9')
             digit = *s - '0';
           else if (*s >= 'a' && *s <= 'f')
             digit = *s - 'a' + 10;
           else if (*s >= 'A' && *s <= 'F')
             digit = *s - 'A' + 10;
           else {
             if (((*s == ':' && d < 2) || (*s == '.' && d == 2)) && (p == 1 || (d < 3 && p == 2) || (d == 0 && (p == 3 || p == 4))) && d < 3) {
               p = 0;
               ++d;
               continue;
             } else
               break;
           }

           if ((d == 0 && p == 4) || (d > 0 && p == 2) || (d == 3 && p == 1) || d == 4)
               break;

           data[d] = (p ? data[d] << 4 : 0) + digit;
           ++p;
        }

        if (s == end && p != 0 && d != 3) {
            ctx.error("invalid id or invalid PCI address format");
            return false;
        }

        port_id = DPDKDevice::get_port_from_pci(data[0],data[1],data[2],data[3]);
    }

    if (port_id >= 0 && port_id < rte_eth_dev_count()){
        result = DPDKDevice::get_device(port_id);
    }
    else {
        ctx.error("Cannot resolve PCI address to DPDK device");
        return false;
    }

    return true;
}

#if HAVE_DPDK_PACKET_POOL
int DPDKDevice::NB_MBUF = 32*4096*2; //Must be able to fill the packet data pool, and then have some packets for IO
#else
int DPDKDevice::NB_MBUF = 65536;
#endif
#ifdef RTE_MBUF_DEFAULT_BUF_SIZE
int DPDKDevice::MBUF_DATA_SIZE = RTE_MBUF_DEFAULT_BUF_SIZE;
#else
int DPDKDevice::MBUF_DATA_SIZE = 2048 + RTE_PKTMBUF_HEADROOM;
#endif
int DPDKDevice::MBUF_SIZE = MBUF_DATA_SIZE 
                          + sizeof (struct rte_mbuf);
int DPDKDevice::MBUF_CACHE_SIZE = 256;
int DPDKDevice::RX_PTHRESH = 8;
int DPDKDevice::RX_HTHRESH = 8;
int DPDKDevice::RX_WTHRESH = 4;
int DPDKDevice::TX_PTHRESH = 36;
int DPDKDevice::TX_HTHRESH = 0;
int DPDKDevice::TX_WTHRESH = 0;
String DPDKDevice::MEMPOOL_PREFIX = "click_mempool_";

unsigned DPDKDevice::DEF_RING_NDESC = 1024;
unsigned DPDKDevice::DEF_BURST_SIZE = 32;

unsigned DPDKDevice::RING_FLAGS = 0;
unsigned DPDKDevice::RING_SIZE  = 64;
unsigned DPDKDevice::RING_POOL_CACHE_SIZE = 32;
unsigned DPDKDevice::RING_PRIV_DATA_SIZE  = 0;

bool DPDKDevice::_is_initialized = false;
HashTable<uint8_t, DPDKDevice> DPDKDevice::_devs;
struct rte_mempool** DPDKDevice::_pktmbuf_pools;
unsigned DPDKDevice::_nr_pktmbuf_pools;
bool DPDKDevice::no_more_buffer_msg_printed = false;

CLICK_ENDDECLS
