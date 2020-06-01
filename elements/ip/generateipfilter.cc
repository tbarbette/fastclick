// -*- c-basic-offset: 4; related-file-name: "generateipfilter.hh" -*-
/*
 * generateipfilter.{cc,hh} -- element generates IPFilter patterns out of input traffic
 * Tom Barbette, (extended by) Georgios Katsikas
 *
 * Copyright (c) 2017 Tom Barbette, University of Liège
 * Copyright (c) 2017 Georgios Katsikas, RISE SICS
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

#include "generateipfilter.hh"

CLICK_DECLS

const int GenerateIPPacket::DEF_NB_RULES = 1000;
const uint16_t GenerateIPPacket::INCLUDE_TP_PORT = 0xffff;

/**
 * Base class for pattern generation out of incoming traffic.
 */
GenerateIPPacket::GenerateIPPacket() : _nrules(DEF_NB_RULES), _flows_nb(0), _inst_rules(0), _prefix(32)
{
}

GenerateIPPacket::~GenerateIPPacket()
{
}

int
GenerateIPPacket::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
            .read_p("NB_RULES", _nrules)
            .consume() < 0)
        return -1;

    return 0;
}

int
GenerateIPPacket::initialize(ErrorHandler *errh)
{
    return 0;
}

int
GenerateIPPacket::build_mask(IPFlowID &mask, bool keep_saddr, bool keep_daddr, bool keep_sport, bool keep_dport, int prefix)
{
    if (!keep_saddr && !keep_daddr && !keep_sport && !keep_dport) {
        return -1;
    }

     mask = IPFlowID(
        (keep_saddr ? IPAddress::make_prefix(prefix) : IPAddress("")),
        (keep_sport ? INCLUDE_TP_PORT : 0),
        (keep_daddr ? IPAddress::make_prefix(prefix) : IPAddress("")),
        (keep_dport ? INCLUDE_TP_PORT : 0)
    );

     return 0;
}

void
GenerateIPPacket::cleanup(CleanupStage)
{
    return;
}

HashMap<uint8_t, RuleFormatter *> GenerateIPFilter::_rule_formatter_map;

/**
 * IP FIlter rules' generator out of incoming traffic.
 */
GenerateIPFilter::GenerateIPFilter() :
    GenerateIPPacket(), _keep_saddr(true), _keep_daddr(true), _keep_sport(false), _keep_dport(true),
    _map_reduced(false), _pref_reduced(-1), _pattern_type(NONE), _out_file("rules")
{
}



GenerateIPFilter::GenerateIPFilter(RulePattern pattern_type) :
    GenerateIPPacket(), _keep_saddr(true), _keep_daddr(true), _keep_sport(false), _keep_dport(true),
    _map_reduced(false), _pref_reduced(-1), _out_file("rules")
{
    _pattern_type = pattern_type;
}

GenerateIPFilter::~GenerateIPFilter()
{
    auto it = _rule_formatter_map.begin();
    while (it != _rule_formatter_map.end()) {
        delete it.value();
        it++;
    }
    _rule_formatter_map.clear();
}

int
GenerateIPFilter::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String pattern_type = "IPFILTER";

    if (Args(conf, this, errh)
            .read("KEEP_SADDR", _keep_saddr)
            .read("KEEP_DADDR", _keep_daddr)
            .read("KEEP_SPORT", _keep_sport)
            .read("KEEP_DPORT", _keep_dport)
            .read("PREFIX", _prefix)
            .read("PATTERN_TYPE", pattern_type)
            .read("OUT_FILE", _out_file)
            .consume() < 0)
        return -1;

    if (_pattern_type == NONE) {
        if (pattern_type.upper() == "IPFILTER") {
            _pattern_type = IPFILTER;
        } else if (pattern_type.upper() == "IPCLASSIFIER") {
            _pattern_type = IPCLASSIFIER;
        } else {
            errh->error("Invalid PATTERN_TYPE for GenerateIPFilter. Select in [IPFILTER, IPCLASSIFIER]");
            return -1;
        }
    }

    int status = build_mask(_mask, _keep_saddr, _keep_daddr, _keep_sport, _keep_dport, _prefix);
    if (status != 0) {
        return errh->error("Cannot continue with empty mask");
    }

    // Create the supported rule formatter
    if (_pattern_type == IPFILTER) {
        _rule_formatter_map.insert(
            static_cast<uint8_t>(RULE_IPFILTER),
            new IPFilterRuleFormatter(_keep_sport, _keep_dport));
    } else if (_pattern_type == IPCLASSIFIER) {
        _rule_formatter_map.insert(
            static_cast<uint8_t>(RULE_IPCLASSIFIER),
            new IPClassifierRuleFormatter(_keep_sport, _keep_dport));
    }

    return GenerateIPPacket::configure(conf, errh);
}

int
GenerateIPFilter::initialize(ErrorHandler *errh)
{
    return GenerateIPPacket::initialize(errh);
}

void
GenerateIPFilter::cleanup(CleanupStage)
{
}

Packet *
GenerateIPFilter::simple_action(Packet *p)
{
    // Create a flow signature for this packet
    IPFlowID flowid(p);
    IPFlow new_flow = IPFlow();
    new_flow.initialize(flowid & _mask);

    // Check if we already have such a flow
    IPFlow *found = _map.find(flowid).get();

    // New flow
    if (!found) {
        // Its length is the length of this packet
        new_flow.update_flow_size(p->length());

        // Keep the protocol type
        new_flow.set_proto(p->ip_header()->ip_p);

        // new_flow.print_flow_info();

        // Insert this new flow into the flow map
        _map.find_insert(new_flow);

        _flows_nb++;
    } else {
        // Aggregate
        found->update_flow_size(p->length());
    }

    return p;
}

#if HAVE_BATCH
PacketBatch*
GenerateIPFilter::simple_action_batch(PacketBatch *batch)
{
    EXECUTE_FOR_EACH_PACKET(GenerateIPFilter::simple_action, batch);
    return batch;
}
#endif

IPFlowID
GenerateIPFilter::get_mask(int prefix)
{
    IPFlowID fid = IPFlowID(IPAddress::make_prefix(prefix), _mask.sport(), IPAddress::make_prefix(prefix), _mask.dport());
    return fid;
}

bool
GenerateIPFilter::is_wildcard(const IPFlow &flow)
{
    if ((flow.flowid().saddr().s() == "0.0.0.0") ||
        (flow.flowid().daddr().s() == "0.0.0.0")) {
        return true;
    }

    return false;
}

int
GenerateIPFilter::prepare_rules(bool verbose)
{
    // This method must be called only once
    if (_map_reduced) {
        assert((_pref_reduced > 0) && (_pref_reduced <= 32));
        return _pref_reduced;
    }
    assert(!_map_reduced && _pref_reduced < 0);

    Timestamp before = Timestamp::now();

    uint8_t n = 0;

    while (_map.size() > (unsigned)_nrules) {
        HashTable<IPFlow> new_map;

        if (verbose) {
            click_chatter("%8d rules with prefix /%02d, continuing with /%02d", _map.size(), 32 - n, 32 - n - 1);
        }

        ++n;
        _mask = this->get_mask(32 - n);

        for (auto flow : _map) {
            // Wildcards are intentionally excluded
            if (is_wildcard(flow)) {
                continue;
            }

            flow.set_mask(_mask);
            IPFlow *found = new_map.find(flow.flowid()).get();

            // New flow
            if (!found) {
                // Insert this new flow into the flow map
                new_map.find_insert(flow);
            } else {
                // Aggregate
                found->update_flow_size(flow.flow_size());
            }
        }

        // Replace the map only if the new one is a compressed version of the current
        if ((new_map.size() == 0) || ((new_map.size() >= _map.size()) && (new_map.size() > _nrules))) {
            n--;
            break;
        } else {
            _map = new_map;
        }

        if (n == 32) {
            click_chatter("Impossible to reduce the number of rules below: %d" + _map.size());
            return -1;
        }
    }

    _map_reduced = true;
    _pref_reduced = (int) (32 - n);

    if (verbose) {
        click_chatter("%8d rules with prefix /%02d", _map.size(), _pref_reduced);
    }

    Timestamp after = Timestamp::now();
    uint32_t elapsed_sec = (after - before).sec();
    uint32_t elapsed_usec = (after - before).usec();
    uint32_t elapsed_msec = (after - before).msec();

    if (verbose) {
        click_chatter("\n");
        click_chatter(
            "Time to create the flow map: %" PRIu32 " micro sec. (%" PRIu32 " milli sec. %" PRIu32 " sec. or %.2f min.)",
            elapsed_usec, elapsed_msec, elapsed_sec, (float) elapsed_sec / (float) 60
        );
    }

    assert((_pref_reduced > 0) && (_pref_reduced <= 32));
    return _pref_reduced;
}

int
GenerateIPFilter::count_rules()
{
    // Create the map of flows if not already there
    if (_pref_reduced < 0) {
        prepare_rules(true);
    }

    _inst_rules = _map.size();

    return _inst_rules;
}

String
GenerateIPFilter::dump_rules(const RuleFormat& rf, bool verbose)
{
    // Create the map of flows if not already there
    int n = _pref_reduced;
    if (n < 0) {
        n = prepare_rules(verbose);
    }

    RuleFormatter *rule_f = _rule_formatter_map[static_cast<uint8_t>(rf)];
    assert(rule_f);

    uint32_t flow_nb = 0;
    StringAccum acc;

    for (auto flow : _map) {
        String rule = rule_f->flow_to_string(flow, flow_nb++, (uint8_t) n);
        if (!rule.empty()) {
            acc << rule;
        }
    }

    return acc.take_string();
}

String
GenerateIPFilter::read_handler(Element *e, void *user_data)
{
    GenerateIPFilter *g = static_cast<GenerateIPFilter *>(e);
    if (!g) {
        return "GenerateIPFilter element not found";
    }

    assert(g->_pattern_type == IPFILTER || g->_pattern_type == IPCLASSIFIER);
    RuleFormat rf = RULE_IPFILTER;
    if (g->_pattern_type == IPCLASSIFIER) {
        rf = RULE_IPCLASSIFIER;
    }

    intptr_t what = reinterpret_cast<intptr_t>(user_data);

    switch (what) {
        case h_flows_nb: {
            return String(g->_flows_nb);
        }
        case h_rules_nb: {
            return String(g->count_rules());
        }
        case h_dump: {
            return g->dump_rules(rf, true);
        }
        default: {
            click_chatter("Unknown read handler: %d", what);
            return "";
        }
    }
}

int
GenerateIPFilter::dump_rules_to_file(const String &content)
{
    FILE *fp = NULL;

    fp = fopen(_out_file.c_str(), "w");
    if (fp == NULL) {
        click_chatter("Failed to open file: %s", _out_file.c_str());
        return -1;
    }

    fputs(content.c_str(), fp);

    // Close the file
    fclose(fp);

    return 0;
}

String
GenerateIPFilter::to_file_handler(Element *e, void *user_data)
{
    GenerateIPFilter *g = static_cast<GenerateIPFilter *>(e);
    if (!g) {
        return "GenerateIPFilter element not found";
    }
    RuleFormat rf = RULE_IPFILTER;
    if (g->_pattern_type == IPCLASSIFIER) {
        rf = RULE_IPCLASSIFIER;
    }

    intptr_t what = reinterpret_cast<intptr_t>(user_data);

    String rules = g->dump_rules(rf, true);
    if (rules.empty()) {
        click_chatter("No rules to write to file: %s", g->_out_file.c_str());
        return "";
    }

    if (g->dump_rules_to_file(rules) != 0) {
        return "";
    }

    return "";
}

String
IPClassifierRuleFormatter::flow_to_string(GenerateIPPacket::IPFlow &flow, const uint32_t flow_nb, const uint8_t prefix)
{
    assert((prefix > 0) && (prefix <= 32));
    StringAccum acc;

    acc << "src net " << flow.flowid().saddr() << '/' << (int) prefix << " && "
        << "dst net " << flow.flowid().daddr() << '/' << (int) prefix;
    if (_with_tp_s_port)
        acc << " && src port " << flow.flowid().sport_host_order();
    if (_with_tp_d_port) {
        acc << " && dst port " << flow.flowid().dport_host_order();
    }
    acc << ",\n";

    return acc.take_string();
}

String
IPFilterRuleFormatter::flow_to_string(GenerateIPPacket::IPFlow &flow, const uint32_t flow_nb, const uint8_t prefix)
{
    String rule = IPClassifierRuleFormatter::flow_to_string(flow, flow_nb, prefix);
    if (rule.empty()) {
        return rule;
    }

    return "allow " + rule;
}

void
GenerateIPFilter::add_handlers()
{
    add_read_handler("flows_nb", read_handler, h_flows_nb);
    add_read_handler("rules_nb", read_handler, h_rules_nb);
    add_read_handler("dump", read_handler, h_dump);
    add_read_handler("dump_to_file", to_file_handler, h_dump_to_file);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(GenerateIPFilter)
