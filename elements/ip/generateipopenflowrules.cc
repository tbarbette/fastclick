// -*- c-basic-offset: 4; related-file-name: "generateopenflow.hh" -*-
/*
 * generateipopenflowrules.{cc,hh} -- element generates OpenFlow rules out of input traffic
 * Tom Barbette, Georgios Katsikas
 *
 * Copyright (c) 2020 Georgios Katsikas, UBITECH and KTH Royal Institute of Technology
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
#include <click/glue.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/straccum.hh>

#include "generateipopenflowrules.hh"

CLICK_DECLS

static const uint8_t DEF_IN_PORT = 1;
static const uint8_t DEF_OUT_PORT = 1;
static const uint8_t DEF_OF_TABLE = 0;
static const uint8_t MIN_OF_VER = 0;
static const uint8_t MAX_OF_VER = 5;

/**
 * OpenFlow rules' generator out of incoming traffic.
 */
GenerateIPOpenFlowRules::GenerateIPOpenFlowRules() : GenerateIPFilter(OPENFLOW)
{
}

GenerateIPOpenFlowRules::~GenerateIPOpenFlowRules()
{
}

int
GenerateIPOpenFlowRules::configure(Vector<String> &conf, ErrorHandler *errh)
{
    int of_proto_ver;
    String of_bridge;
    uint16_t of_table = DEF_OF_TABLE;
    uint8_t in_port = DEF_IN_PORT;
    uint8_t out_port = DEF_OUT_PORT;

    if (Args(conf, this, errh)
            .read_mp("OF_PROTO", of_proto_ver)
            .read_mp("OF_BRIDGE", of_bridge)
            .read_p("OF_TABLE", of_table)
            .read_p("IN_PORT", in_port)
            .read_p("OUT_PORT", out_port)
            .consume() < 0)
        return -1;

    if ((of_proto_ver < 0) || (of_proto_ver > MAX_OF_VER)) {
        return errh->error("Unsupported OpenFlow protocol version. Select one in [%d, %d]", MIN_OF_VER, MAX_OF_VER);
    }

    if (of_bridge.empty()) {
        return errh->error("Invalid OpenFlow bridge name");
    }

    if (of_table < 0) {
        return errh->error("Invalid OpenFlow table: %d", of_table);
    }

    if (in_port < 0) {
        return errh->error("Invalid IN_PORT. Input a non-negative integer.");
    }

    if (out_port < 0) {
        return errh->error("Invalid OUT_PORT. Input a non-negative integer.");
    }

    if (GenerateIPFilter::configure(conf, errh) < 0) {
        return -1;
    }

    int status = build_mask(_mask, _keep_saddr, _keep_daddr, _keep_sport, _keep_dport, _prefix);
    if (status != 0) {
        return errh->error("Cannot continue with empty mask");
    }

    // Create the supported OpenFlow rule formatters
    _rule_formatter_map.insert(
        static_cast<uint8_t>(RULE_OF_OVS),
        new OVSOpenFlowRuleFormatter(
            static_cast<OpenFlowProtoVersion>(of_proto_ver), of_bridge, of_table, in_port, out_port, _keep_sport, _keep_dport));
    _rule_formatter_map.insert(
        static_cast<uint8_t>(RULE_OF_ONOS),
        new ONOSOpenFlowRuleFormatter(
            static_cast<OpenFlowProtoVersion>(of_proto_ver), of_bridge, of_table, in_port, out_port, _keep_sport, _keep_dport));

    return 0;
}

int
GenerateIPOpenFlowRules::initialize(ErrorHandler *errh)
{
    return GenerateIPPacket::initialize(errh);
}

void
GenerateIPOpenFlowRules::cleanup(CleanupStage)
{
}

IPFlowID
GenerateIPOpenFlowRules::get_mask(int prefix)
{
    IPFlowID fid = IPFlowID(IPAddress::make_prefix(prefix), _mask.sport(), IPAddress::make_prefix(prefix), _mask.dport());
    return fid;
}

bool
GenerateIPOpenFlowRules::is_wildcard(const IPFlow &flow)
{
    if ((flow.flowid().saddr().s() == "0.0.0.0") ||
        (flow.flowid().daddr().s() == "0.0.0.0")) {
        return true;
    }

    return false;
}

String
GenerateIPOpenFlowRules::dump_ovs_rules(bool verbose)
{
    return dump_rules(RULE_OF_OVS, verbose);
}

String
GenerateIPOpenFlowRules::dump_onos_rules(bool verbose)
{
    click_chatter("Currently unsupported feature, coming soon");
    return dump_rules(RULE_OF_ONOS, verbose);
}

String
GenerateIPOpenFlowRules::read_handler(Element *e, void *user_data)
{
    GenerateIPOpenFlowRules *g = static_cast<GenerateIPOpenFlowRules *>(e);
    if (!g) {
        return "GenerateIPOpenFlowRules element not found";
    }

    assert(g->_pattern_type == OPENFLOW);

    intptr_t what = reinterpret_cast<intptr_t>(user_data);

    switch (what) {
        case h_flows_nb: {
            return String(g->_flows_nb);
        }
        case h_rules_nb: {
            return String(g->count_rules());
        }
        case h_dump: {
            return g->dump_ovs_rules(true);
        }
        case h_dump_ovs: {
            return g->dump_ovs_rules(true);
        }
        case h_dump_onos: {
            return g->dump_onos_rules(true);
        }
        default: {
            click_chatter("Unknown read handler: %d", what);
            return "";
        }
    }
}

String
GenerateIPOpenFlowRules::to_file_handler(Element *e, void *user_data)
{
    GenerateIPOpenFlowRules *g = static_cast<GenerateIPOpenFlowRules *>(e);
    if (!g) {
        return "GenerateIPOpenFlowRules element not found";
    }

    intptr_t what = reinterpret_cast<intptr_t>(user_data);

    String rules = "";
    switch (what) {
        case h_dump_ovs: {
            rules = g->dump_ovs_rules(true);
            break;
        }
        case h_dump_onos: {
            rules = g->dump_onos_rules(true);
            break;
        }
        default: {
            rules = "";
            break;
        }
    }
    if (rules.empty()) {
        click_chatter("No rules to write to file: %s", g->_out_file.c_str());
        return "";
    }

    if (g->dump_rules_to_file(rules) != 0) {
        return "";
    }

    return "";
}

void
GenerateIPOpenFlowRules::add_handlers()
{
    add_read_handler("flows_nb", read_handler, h_flows_nb);
    add_read_handler("rules_nb", read_handler, h_rules_nb);
    add_read_handler("dump", read_handler, h_dump);
    add_read_handler("dump_ovs", read_handler, h_dump_ovs);
    add_read_handler("dump_onos", read_handler, h_dump_onos);
    add_read_handler("dump_to_file", to_file_handler, h_dump_ovs);
    add_read_handler("dump_ovs_to_file", to_file_handler, h_dump_ovs);
    add_read_handler("dump_onos_to_file", to_file_handler, h_dump_onos);
}

String
OVSOpenFlowRuleFormatter::flow_to_string(GenerateIPPacket::IPFlow &flow, const uint32_t flow_nb, const uint8_t prefix)
{
    StringAccum acc;
    acc << "ovs-ofctl -O OpenFlow1" << _of_proto_ver << " ";
    acc << "add-flow " << _of_bridge << " ";
    acc << "\"table=" << _of_table << ", ";
    acc << "priority=0, "; // Use a fixed priority for now. Could be another input argument

    // Match operations
    acc << "in_port=" << (int) _in_port << ", ";
    acc << "eth_type=2048, "; // We assume IPv4 for now
    int proto = (int) flow.flow_proto();
    if (proto > 0) {
        switch(proto) {
            case 1:
                acc << "icmp, ";
                break;
            case 6:
                acc << "tcp, ";
                break;
            case 17:
                acc << "udp, ";
                break;
            default:
                click_chatter("Unsupported IP protocol: %d", proto);
                return "";
        }
    }

    bool no_ips = false;
    if ((flow.flowid().saddr().s() != "0.0.0.0")) {
        acc << "nw_src=" << flow.flowid().saddr() << '/' << (int) prefix << ", ";
    } else {
        no_ips = true;
        click_chatter("Broadcast source IP address ignored");
    }
    if ((flow.flowid().daddr().s() != "0.0.0.0")) {
        acc << "nw_dst=" << flow.flowid().daddr() << '/' << (int) prefix << ", ";
    } else {
        no_ips = true;
        click_chatter("Broadcast destination IP address ignored");
    }

    if (no_ips) {
        return "";
    }

    if ((proto == 6) || (proto == 17)) {
        if (_with_tp_s_port) {
            acc << "tp_src=" << flow.flowid().sport_host_order() << ", ";
        }
        if (_with_tp_d_port) {
            acc << "tp_dst=" << flow.flowid().dport_host_order() << ", ";
        }
    }

    // Actions
    acc << "actions=";
    acc << "output:";
    if (_in_port == _out_port) {
        acc << "in_port";
    } else {
        acc << (int) _out_port;
    }
    acc << "\"\n";

    return acc.take_string();
}

String
ONOSOpenFlowRuleFormatter::flow_to_string(GenerateIPPacket::IPFlow &flow, const uint32_t flow_nb, const uint8_t prefix)
{
    StringAccum acc;

    // TODO

    return acc.take_string();
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(GenerateIPFilter)
EXPORT_ELEMENT(GenerateIPOpenFlowRules)
