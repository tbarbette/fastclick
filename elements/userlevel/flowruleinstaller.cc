// -*- c-basic-offset: 4; related-file-name: "flowruleinstaller.hh" -*-
/*
 * flowruleinstaller.{cc,hh} -- element that performs DPDK Flow API installations
 * and/or updates.
 *
 * Copyright (c) 2020 Tom Barbette, KTH Royal Institute of Technology
 * Copyright (c) 2020 Georgios Katsikas, KTH Royal Institute of Technology
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

#include <click/args.hh>
#include <click/error.hh>
#include <click/router.hh>
#include <click/straccum.hh>
#include <click/userutils.hh>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <signal.h>
#include <limits>

#include "todpdkdevice.hh"
#include "flowruleinstaller.hh"

CLICK_DECLS

/******************************
 * FlowRuleInstaller
 ******************************/
/**
 * FlowRuleInstaller constructor.
 */
FlowRuleInstaller::FlowRuleInstaller() :
    _timer(this), _verbose(false)
{
    _group = 1;
    _core_id = click_max_cpu_ids() - 1;
}

/**
 * FlowRuleInstaller destructor.
 */
FlowRuleInstaller::~FlowRuleInstaller()
{
    //Cleanup() does the cleanup
}

/**
 * Configures FlowRuleInstaller according to user inputs.
 */
int
FlowRuleInstaller::configure(Vector<String> &conf, ErrorHandler *errh)
{
    Element * nic;

    if (Args(conf, this, errh)
        .read    ("NIC",           nic)
	.read    ("GROUP",         _group)
        .read    ("PIN_TO_CORE",   _core_id)
        .read    ("VERBOSE",       _verbose)
        .complete() < 0)
        return -1;

    // The CPU core ID where this element will be pinned
    unsigned max_core_nb = click_max_cpu_ids();
    if ((_core_id < 0) || (_core_id >= max_core_nb)) {
        return errh->error(
            "Cannot pin FlowRuleInstaller agent to CPU core: %d. "
            "Use a CPU core index in [0, %d].",
            _core_id, max_core_nb
        );
    }

    // Setup pointers with the underlying NICs
    _fd = (FromDPDKDevice*)nic->cast("FromDPDKDevice");

    return 0;
}

/**
 * Allocates memory resources before the start up.
 */
int
FlowRuleInstaller::initialize(ErrorHandler *errh)
{
    _timer.initialize(this);
    _timer.move_thread(_core_id);

    click_chatter("Successful initialization! \n\n");

    return 0;
}

/**
 * Cleans up static resources.
 */
int
FlowRuleInstaller::static_cleanup()
{
    return 0;
}

/**
 * Releases memory resources before exiting.
 */
void
FlowRuleInstaller::cleanup(CleanupStage)
{
    // Delete stored rules
    Vector<struct rte_flow *> &port_rule_list = _rules;
    for (auto it = port_rule_list.begin(); it != port_rule_list.end(); ++it) {
        delete it;
    }
    _rules.clear();
}

String
FlowRuleInstaller::get_nic_name_from_handler_input(String &input)
{
    int delim = input.find_left(' ');
    // Only one argument was given
    if (delim < 0) {
        click_chatter("Handler requires a NIC name first and the rest of the argument");
        return "";
    }
    return input.substring(0, delim).trim_space_left();
}

int
FlowRuleInstaller::store_inserted_rule(struct rte_flow *rule)
{
    _rules.push_back(rule);

    return 0;
}

Vector<rte_flow_item>
FlowRuleInstaller::parse_5t(Vector<String> words, bool is_tcp, bool have_ports)
{
    Vector<rte_flow_item> pattern;
    {
        rte_flow_item pat;
        pat.type = RTE_FLOW_ITEM_TYPE_IPV4;
        struct rte_flow_item_ipv4* spec = (struct rte_flow_item_ipv4*) malloc(sizeof(rte_flow_item_ipv4));
        struct rte_flow_item_ipv4* mask = (struct rte_flow_item_ipv4*) malloc(sizeof(rte_flow_item_ipv4));

        bzero(spec, sizeof(rte_flow_item_ipv4));
        bzero(mask, sizeof(rte_flow_item_ipv4));
        Vector<String> src = words[1].split('%');
        Vector<String> dst = words[have_ports?3:2].split('%');
        //printf("Pat %s %s\n",src[0].c_str(), dst[0].c_str());
        spec->hdr.src_addr = IPAddress(src[0]);
        mask->hdr.src_addr = src.size() > 1?IPAddress(src[1]) : IPAddress(-1);
        spec->hdr.dst_addr = IPAddress(dst[0]);
        mask->hdr.dst_addr = dst.size() > 1?IPAddress(dst[1]) : IPAddress(-1);
        pat.spec = spec;
        pat.mask = mask;
        pat.last = 0;
        pattern.push_back(pat);
    }
    if (have_ports && is_tcp)
    {
        rte_flow_item pat;
        pat.type = RTE_FLOW_ITEM_TYPE_TCP;
        struct rte_flow_item_tcp* spec = (struct rte_flow_item_tcp*) malloc(sizeof(rte_flow_item_tcp));
        struct rte_flow_item_tcp* mask = (struct rte_flow_item_tcp*) malloc(sizeof(rte_flow_item_tcp));
        bzero(spec, sizeof(rte_flow_item_tcp));
        bzero(mask, sizeof(rte_flow_item_tcp));
        spec->hdr.src_port = atoi(words[2].c_str());
        mask->hdr.src_port = -1;
        spec->hdr.dst_port = atoi(words[4].c_str());
        mask->hdr.dst_port = -1;
        pat.spec = spec;
        pat.mask = mask;
        pat.last = 0;
        pattern.push_back(pat);
    }
    else if (have_ports && !is_tcp)
    {
        rte_flow_item pat;
        pat.type = RTE_FLOW_ITEM_TYPE_UDP;
        struct rte_flow_item_udp* spec = (struct rte_flow_item_udp*) malloc(sizeof(rte_flow_item_udp));
        struct rte_flow_item_udp* mask = (struct rte_flow_item_udp*) malloc(sizeof(rte_flow_item_udp));
        bzero(spec, sizeof(rte_flow_item_udp));
        bzero(mask, sizeof(rte_flow_item_udp));
        spec->hdr.src_port = atoi(words[2].c_str());
        mask->hdr.src_port = -1;
        spec->hdr.dst_port = atoi(words[4].c_str());
        mask->hdr.dst_port = -1;
        pat.spec = spec;
        pat.mask = mask;
        pat.last = 0;
        pattern.push_back(pat);
    }

    rte_flow_item end;
    memset(&end, 0, sizeof(struct rte_flow_item));
    end.type =  RTE_FLOW_ITEM_TYPE_END;
    pattern.push_back(end);
    return pattern;
}

struct rte_flow *
FlowRuleInstaller::flow_add_redirect(portid_t port_id, int from, int to, bool validate, int priority)
{
    struct rte_flow_attr attr;
    memset(&attr, 0, sizeof(struct rte_flow_attr));
    attr.ingress = 1;
    attr.group = from;
    attr.priority =  priority;

    struct rte_flow_action action[2];
    struct rte_flow_action_jump jump;

    memset(action, 0, sizeof(struct rte_flow_action) * 2);
    action[0].type = RTE_FLOW_ACTION_TYPE_JUMP;
    action[0].conf = &jump;
    action[1].type = RTE_FLOW_ACTION_TYPE_END;
    jump.group = to;

    Vector<rte_flow_item> pattern;
    rte_flow_item pat;
    pat.type = RTE_FLOW_ITEM_TYPE_ETH;
    pat.spec = 0;
    pat.mask = 0;
    pat.last = 0;
    pattern.push_back(pat);
    rte_flow_item end;
    memset(&end, 0, sizeof(struct rte_flow_item));
    end.type =  RTE_FLOW_ITEM_TYPE_END;
    pattern.push_back(end);

    struct rte_flow_error error;
    int res = 0;
    if (validate) {
        res = rte_flow_validate(port_id, &attr, pattern.data(), action, &error);
        if (res != 0) {
            click_chatter("Flow rule can't be validated %d message: %s\n",
                error.type,
                error.message ? error.message : "(no stated reason)");
            return NULL;
        }
    }
    struct rte_flow *flow = rte_flow_create(port_id, &attr, pattern.data(), action, &error);
    if (!flow) {
        click_chatter("Flow rule can't be created %d message: %s\n",
            error.type,
            error.message ? error.message : "(no stated reason)");
        return NULL;
    }

    return flow;
}

Vector<String>
FlowRuleInstaller::rule_list_generate(const int &rules_nb, bool do_port, String mask)
{
    Vector<String> rules;

    for (int i = 0; i < rules_nb; i++) {
        StringAccum acc;
        IPAddress i_mask = IPAddress(mask);
        IPAddress src =  IPAddress::make_random() & i_mask;
        IPAddress dst =  IPAddress::make_random() & i_mask;
        int sport = 0;

        int dport = 0;
        acc << "udp " << src.unparse() << "%" << mask << " ";
        if (do_port) {
            sport = rand() % 0xfff0;
            acc << String(sport) << " ";
        }
        acc << dst.unparse() << "%" << mask;
        if (do_port) {
            dport = rand() % 0xff0;
            acc << " " << dport;
        }
/*
        IPFlowID tuple = IPFlowID(src | i_mask, sport, dst |i_mask, dport);
        if (set.find(tuple))
            goto again;*/
        rules.push_back(acc.take_string());
    //    click_chatter("%s", rules.back().c_str());

    }

    return rules;
}

struct rte_flow *
FlowRuleInstaller::flow_generate(portid_t port_id, Vector<rte_flow_item> &pattern, int table, int priority, int index)
{
    struct rte_flow_attr attr;
    memset(&attr, 0, sizeof(struct rte_flow_attr));
    attr.ingress = 1;
    attr.group = table;
    attr.priority = priority;

    struct rte_flow_action action[2];
    struct rte_flow_action_queue queue = {.index = (uint16_t)index};

    memset(action, 0, sizeof(struct rte_flow_action) * 2);
    action[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
    action[0].conf = &queue;

    action[1].type = RTE_FLOW_ACTION_TYPE_END;

    struct rte_flow_error error;
    int res = 0;

    res = rte_flow_validate(port_id, &attr, pattern.data(), action, &error);
    if (res == 0) {
        return rte_flow_create(port_id, &attr, pattern.data(), action, &error);
    }
    else  {
        click_chatter("Rule did not validate");
    }

    return NULL;
}

/**
 * Read and write handlers.
 */
enum {
    h_flow_create_5t, h_flow_create_5t_list, h_flow_create_pair, h_flow_create_pair_list,
    h_flow_update_5t, h_flow_jump, h_flow_flush, h_flow_count
};

int FlowRuleInstaller::flow_handler(
        int operation, String &input, Element *e,
        const Handler *handler, ErrorHandler *errh) {

    FlowRuleInstaller *fr = static_cast<FlowRuleInstaller *>(e);
    if (!fr) {
        return errh->error("Invalid flow rule installer instance");
    }

    int delim = input.find_left(' ');
    // Only one argument was given
    if (delim < 0) {
        delim = input.length() - 1;
    }

    portid_t port_id = fr->_fd->get_device()->get_port_id();

    // The rest of the arguments
    String args = input.substring(delim + 1).trim_space_left();

    switch((uintptr_t)handler->read_user_data()) {

        case h_flow_count: {
                input = String(fr->_rules.size());
                return 0;
        }
        case h_flow_create_5t: {
            Vector<String> words = args.split(' ');
            if (words.size() < 5) {
                input = "Usage: tcp|udp ip_src port_src ip_dst port_dst [table]";
                return -1;
            }
            bool is_tcp;
            if (words[0] == "tcp") {
                is_tcp = true;
            } else if (words[0] == "udp") {
                is_tcp = false;
            } else {
                input = "Protocol must be tcp or udp";
                return -1;
            }

            Vector<rte_flow_item> pattern = parse_5t(words);

            struct rte_flow *flow = flow_generate(port_id, pattern, words.size()>3?atoi(words[3].c_str()) : 1, 1, 0);
            if (!flow) {
                input = "Failed to insert rule: " + input;
                return -1;
            }

            if (fr->store_inserted_rule(flow) != 0) {
                input = "Failed to insert rule";
                return -1;
            }

            if (fr->_verbose) {
                click_chatter("Rule inserted %p", flow);
            }

            input = String((uintptr_t) flow);
            return 0;
        }
        case h_flow_create_pair: {
            Vector<String> words = args.split(' ');
            if (words.size() < 3) {
                click_chatter("Usage: tcp|udp ip_src ip_dst [table]");
                return -1;
            }

            bool is_tcp;
            if (words[0] == "tcp") {
                is_tcp = true;
            } else if (words[0] == "udp") {
                is_tcp = false;
            } else {
                click_chatter("Protocol must be tcp or udp");
                return -1;
            }

            Vector<rte_flow_item> pattern = parse_5t(words, is_tcp, false);

            int table = words.size()>3?atoi(words[3].c_str()) : 1;
            int priority = words.size()>4?atoi(words[4].c_str()) : 0;
            struct rte_flow *flow = flow_generate(port_id, pattern, table, priority, 1-priority);
            if (!flow) {
                click_chatter("Failed to insert rule: ");
                return -1;
            }

            if (fr->store_inserted_rule(flow) != 0) {
                input = "Failed to insert rule";
                return -1;
            }

            if (fr->_verbose) {
                click_chatter("Rule inserted %p", flow);
            }

            input = String((uintptr_t) flow);
            return 0;
        }

        case h_flow_create_5t_list: {

            Vector<String> words = args.split(' ');
            int rules_nb = atoi(words[0].c_str());

            if (fr->_verbose)
                click_chatter("Generating %d 5-tuple rules\n", rules_nb);

            int table = words.size()>1?atoi(words[1].c_str()) : 1    ;

            String mask = words.size()>2?words[2]:"255.255.255.255";
            int i = 0;
            Vector<String> rules = rule_list_generate(rules_nb, true, mask);
            for (String rule : rules) {
                Vector<String> words = rule.split(' ');
                bool is_tcp = false;
                Vector<rte_flow_item> pattern = parse_5t(words, false, true);
                struct rte_flow *flow = flow_generate(port_id, pattern, table, 0, 1);
                if (!flow) {
                    click_chatter("Failed to insert rule %d", i);
                    return -1;
                }

                if (fr->store_inserted_rule(flow) != 0) {
                    click_chatter("Failed to insert rule %d", i);
                    return -1;
                }

                if (fr->_verbose) {
                    click_chatter("[Rule %d]: Inserted at %p", i, flow);
                }
                i++;
            }

            return 0;
        }
        case h_flow_create_pair_list: {

            Vector<String> words = args.split(' ');
            int rules_nb = atoi(words[0].c_str());

            if (fr->_verbose)
                click_chatter("Generating %d pair-tuple rules\n", rules_nb);

            int table = words.size()>1?atoi(words[1].c_str()) : 1    ;
            String mask = words.size()>2?words[2]:"255.255.255.255";
            int i = 0;
            Vector<String> rules = rule_list_generate(rules_nb, false, mask);
            for (String rule : rules) {
                Vector<String> words = rule.split(' ');
                bool is_tcp = false;
                Vector<rte_flow_item> pattern = parse_5t(words, false, false);
                struct rte_flow *flow = flow_generate(port_id, pattern, table, 0, 1);
                if (!flow) {
                    click_chatter("Failed to insert rule %d", i);
                    return -1;
                }

                if (fr->store_inserted_rule(flow) != 0) {
                    click_chatter("Failed to insert rule %d", i);
                    return -1;
                }

                if (fr->_verbose) {
                    click_chatter("[Rule %d]: Inserted at %p", i, flow);
                }
                i++;
            }

            return 0;
        }

        case h_flow_update_5t: {
            Vector<String> words = args.split(' ');

            if (words.size() != 5) {
                input = "Arguments must be 5: rule_id ip_src port_src ip_dst port_dst";
                return -1;
            }

            rte_flow *rule = (rte_flow*)(uintptr_t)atoll(words[0].c_str());
            Vector<rte_flow_item> pattern = parse_5t(words);

            struct rte_flow_error error;
            click_chatter("Updating port %p\n", rule);
            // int res = rte_flow_update(port_id, rule, pattern.data(), 0, &error);
            int res = 0;
            if (res == 0) {
                if (fr->_verbose) {
                    click_chatter("Update success");
                }
                return 0;
            } else {
                input = "Failed to update rule";
                return -1;
            }
        }
        case h_flow_jump:{
            Vector<String> groups = args.split(' ');
            if (groups.size() != 2) {
                click_chatter("The argument must be 'from <group> to <another-group>'");
                return -1;
            }
            int from = atoi(groups[0].c_str());
            int to = atoi(groups[1].c_str());
            flow_add_redirect(port_id, from, to, true, 0);
            break;
        }
        case h_flow_flush:
            rte_flow_flush(port_id, 0);
            break;
        default:
            input = "<error>";
            return -1;
    }

    return -1;
}

void FlowRuleInstaller::add_handlers()
{
    set_handler("flow_create_5t_list", Handler::f_read | Handler::f_read_param, flow_handler, h_flow_create_5t_list);

    set_handler("flow_create_pair_list", Handler::f_read | Handler::f_read_param, flow_handler, h_flow_create_pair_list);
    set_handler("flow_create_5t", Handler::f_read | Handler::f_read_param, flow_handler, h_flow_create_5t);

    set_handler("flow_create_pair", Handler::f_read | Handler::f_read_param, flow_handler, h_flow_create_pair);
    set_handler("flow_update_5t", Handler::f_read | Handler::f_read_param, flow_handler, h_flow_update_5t);
    set_handler("flow_jump", Handler::f_read | Handler::f_read_param, flow_handler, h_flow_jump);

    set_handler("flow_flush", Handler::f_read | Handler::f_read_param, flow_handler, h_flow_flush);
    set_handler("flow_count", Handler::f_read | Handler::f_read_param, flow_handler, h_flow_count);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel dpdk Json)

EXPORT_ELEMENT(FlowRuleInstaller)
ELEMENT_MT_SAFE(FlowRuleInstaller)
