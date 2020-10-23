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
 * NIC
 ******************************/
/**
 * NIC constructor.
 */
NIC::NIC(bool verbose) :
    _element(0), _index(-1), _verbose(verbose), mirror(0)
{}

/**
 * NIC copy constructor.
 */
NIC::NIC(const NIC &n)
{
    _index = n._index;
    _verbose = n._verbose;
    _element = n._element;
}

/**
 * NIC destructor.
 */
NIC::~NIC()
{}

/**
 * Sets the Click element that represents this NIC.
 */
void
NIC::set_element(Element *el)
{
    assert(el);
    _element = el;
}

/**
 * Sets the device's Click port index.
 */
void
NIC::set_index(const int &index)
{
    assert(index >= 0);
    _index = index;
}

/**
 * Casts a NIC object to its Click counterpart.
 */
FromDPDKDevice *
NIC::cast()
{
    if (!get_element()) {
        return NULL;
    }
    return dynamic_cast<FromDPDKDevice *>(get_element());
}

/**
 * Returns the number of queues per VF pool.
 */
int
NIC::queue_per_pool()
{
    int nb_vf_pools = atoi(call_rx_read("nb_vf_pools").c_str());
    if (nb_vf_pools == 0) {
        return 1;
    }

    return atoi(call_rx_read("nb_rx_queues").c_str()) / nb_vf_pools;
}

/**
 * Maps a physical CPU core ID to a hardware queue ID.
 */
int
NIC::phys_cpu_to_queue(int phys_cpu_id)
{
    assert(phys_cpu_id >= 0);
    return phys_cpu_id * (queue_per_pool());
}

/**
 * Implements read handlers for a NIC.
 */
String
NIC::call_rx_read(String h)
{
    const Handler *hc = Router::handler(_element, h);

    if (hc && hc->visible()) {
        return hc->call_read(_element, ErrorHandler::default_handler());
    }

    return "undefined";
}

/**
 * Relays handler calls to a NIC's underlying TX element.
 */
String
NIC::call_tx_read(String h)
{
    // TODO: Ensure element type
    ToDPDKDevice *td = dynamic_cast<FromDPDKDevice *>(_element)->find_output_element();
    if (!td) {
        return "[NIC " + String(get_device_address()) + "] Could not find matching ToDPDKDevice";
    }

    const Handler *hc = Router::handler(td, h);
    if (hc && hc->visible()) {
        return hc->call_read(td, ErrorHandler::default_handler());
    }

    return "undefined";
}

/**
 * Relays handler calls to a NIC's underlying Rx element.
 */
int
NIC::call_rx_write(String h, const String input)
{
    FromDPDKDevice *fd = dynamic_cast<FromDPDKDevice *>(_element);
    if (!fd) {
        click_chatter("[NIC %s] Could not find matching FromDPDKDevice", get_device_address().c_str());
        return -1;
    }

    const Handler *hc = Router::handler(fd, h);
    if (hc && hc->visible()) {
        return hc->call_write(input, fd, ErrorHandler::default_handler());
    }

    click_chatter("[NIC %s] Could not find matching handler %s", get_device_address().c_str(), h.c_str());

    return -1;
}


/******************************
 * FlowRuleInstaller
 ******************************/
/**
 * FlowRuleInstaller constructor.
 */
FlowRuleInstaller::FlowRuleInstaller() :
    _timer(this), _timer_period(1000), _verbose(false)
{
    _core_id = click_max_cpu_ids() - 1;
}

/**
 * FlowRuleInstaller destructor.
 */
FlowRuleInstaller::~FlowRuleInstaller()
{
    // Delete stored rules
    auto nic_rules = _rules.begin();
    while (nic_rules != _rules.end()) {
        Vector<struct rte_flow *> port_rule_list = nic_rules.value();
        for (auto it = port_rule_list.begin(); it != port_rule_list.end(); ++it) {
            delete it;
        }
        port_rule_list.clear();
        nic_rules++;
    }
    _rules.clear();

    _nics.clear();
}

/**
 * Configures FlowRuleInstaller according to user inputs.
 */
int
FlowRuleInstaller::configure(Vector<String> &conf, ErrorHandler *errh)
{
    Vector<Element *> nics;

    if (Args(conf, this, errh)
        .read_all("NIC",           nics)
        .read    ("PIN_TO_CORE",   _core_id)
        .read    ("VERBOSE",       _verbose)
        .read    ("TIMER_PERIOD",  _timer_period)
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
    int index = 0;
    for (Element *e : nics) {
        NIC nic(_verbose);
        nic.set_index(index++);
        nic.set_element(e);
        _nics.insert(nic.get_name(), nic);
    }

    return 0;
}

/**
 * Allocates memory resources before the start up
 * and performs controller discovery.
 */
int
FlowRuleInstaller::initialize(ErrorHandler *errh)
{
    _rules.resize(_nics.size());

    _timer.initialize(this);
    _timer.move_thread(_core_id);
    _timer.schedule_after_msec(_timer_period);

    click_chatter("Successful initialization! \n\n");

    return 0;
}

/**
 * Cleans up static resources for service chains.
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
    auto nic_rules = _rules.begin();
    while (nic_rules != _rules.end()) {
        Vector<struct rte_flow *> port_rule_list = nic_rules.value();
        for (auto it = port_rule_list.begin(); it != port_rule_list.end(); ++it) {
            delete it;
        }
        port_rule_list.clear();
        nic_rules++;
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

FromDPDKDevice *
FlowRuleInstaller::get_nic_device_from_handler_input(String &input)
{
    int delim = input.find_left(' ');
    // Only one argument was given
    if (delim < 0) {
        click_chatter("Handler requires a NIC name first and the rest of the argument");
        return NULL;
    }
    String nic_name = get_nic_name_from_handler_input(input);

    NIC *nic = get_nic_by_name(nic_name);
    if (!nic) {
        click_chatter("Invalid NIC %s", nic_name.c_str());
        return NULL;
    }

    FromDPDKDevice *fd = nic->cast();
    if (!fd) {
        click_chatter("Invalid NIC %s", nic_name.c_str());
        return NULL;
    }
    return fd;
}

int
FlowRuleInstaller::store_inserted_rule(portid_t port_id, struct rte_flow *rule)
{
    if (port_id < 0) {
        click_chatter("Cannot store rule on invalid port ID");
        return -1;
    }

    Vector<struct rte_flow *> port_rules = _rules.find(port_id);
    port_rules.push_back(rule);

    return 0;
}

/**
 * FlowRuleInstaller agent's run-time.
 */
void
FlowRuleInstaller::run_timer(Timer *t)
{
    click_chatter("Re-scheduling FlowRuleInstaller");
    _timer.reschedule_after_msec(_timer_period);
}

Vector<rte_flow_item>
FlowRuleInstaller::parse_5t(Vector<String> words)
{
    Vector<rte_flow_item> pattern;
    {
        rte_flow_item pat;
        pat.type = RTE_FLOW_ITEM_TYPE_IPV4;
        struct rte_flow_item_ipv4* spec = (struct rte_flow_item_ipv4*) malloc(sizeof(rte_flow_item_ipv4));
        struct rte_flow_item_ipv4* mask = (struct rte_flow_item_ipv4*) malloc(sizeof(rte_flow_item_ipv4));
        bzero(spec, sizeof(rte_flow_item_ipv4));
        bzero(mask, sizeof(rte_flow_item_ipv4));
        spec->hdr.src_addr = IPAddress(words[1]);
        mask->hdr.src_addr = -1;
        spec->hdr.dst_addr = IPAddress(words[3]);
        mask->hdr.dst_addr = -1;
        pat.spec = spec;
        pat.mask = mask;
        pat.last = 0;
        pattern.push_back(pat);
    }
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

    rte_flow_item end;
    memset(&end, 0, sizeof(struct rte_flow_item));
    end.type =  RTE_FLOW_ITEM_TYPE_END;
    pattern.push_back(end);
    return pattern;
}

struct rte_flow *
FlowRuleInstaller::flow_add_redirect(int port_id, int from, int to, bool validate, int priority)
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
    jump.group=to;

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
    if (validate)
        res = rte_flow_validate(port_id, &attr, pattern.data(), action, &error);
    if (res == 0) {

        struct rte_flow *flow = rte_flow_create(port_id, &attr, pattern.data(), action, &error);
        if (flow)
            click_chatter("Redirect from group %d to group %d with prio %d success", from, to, priority);

        return flow;
    } else {
        if (validate) {
            click_chatter("Rule did not validate.");
        }
        return 0;
    }
}

Vector<String>
FlowRuleInstaller::rule_list_generate(const int &rules_nb)
{
    Vector<String> rules;

    for (int i = 0; i < rules_nb; i++) {
        StringAccum acc;
        acc << "tcp " << IPAddress::make_random().unparse() << " " << rand() % 0xfff0
            << " " << IPAddress::make_random().unparse() << " " << rand() % 0xfff0;
        rules.push_back(acc.take_string());
    }

    return rules;
}

struct rte_flow *
FlowRuleInstaller::flow_generate(portid_t port_id, Vector<rte_flow_item> &pattern)
{
    struct rte_flow_attr attr;
    memset(&attr, 0, sizeof(struct rte_flow_attr));
    attr.ingress = 1;
    attr.group = 1;
    attr.priority = 0;

    struct rte_flow_action action[2];
    struct rte_flow_action_queue queue = {.index = 0};


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

int FlowRuleInstaller::flow_handler(
        int operation, String &input, Element *e,
        const Handler *handler, ErrorHandler *errh) {

    FlowRuleInstaller *fr = static_cast<FlowRuleInstaller *>(e);
    if (!fr) {
        return errh->error("Invalid flow rule installer instance");
    }

    if (input.empty()) {
        input = "Please provide a positive number of rules to generate";
        return errh->error("%s", input.c_str());
    }

    int delim = input.find_left(' ');
    // Only one argument was given
    if (delim < 0) {
        return errh->error("Handler requires a NIC name first and the rest of the arguments");
    }

    String nic_name = fr->get_nic_name_from_handler_input(input);
    FromDPDKDevice *fd = fr->get_nic_device_from_handler_input(input);
    if (!fd) {
        return errh->error("Invalid NIC %s", nic_name.c_str());
    }
    portid_t port_id = fd->get_device()->get_port_id();

    // The rest of the arguments
    String args = input.substring(delim + 1).trim_space_left();

    switch((uintptr_t)handler->read_user_data()) {
        case h_flow_create_5t: {
            Vector<String> words = args.split(' ');
            if (words.size() != 5) {
                input = "Usage: tcp|udp ip_src port_src ip_dst port_dst";
                return -1;
            }
            if (words[0] == "tcp") {

            } else if (words[0] == "udp") {
                input = "Only TCP supported for now";
                return -1;
            } else {
                input = "Protocol must be tcp or udp";
                return -1;
            }

            Vector<rte_flow_item> pattern = parse_5t(words);

            struct rte_flow *flow = flow_generate(port_id, pattern);
            if (!flow) {
                input = "Failed to insert rule: " + input;
                return -1;
            }

            if (fr->store_inserted_rule(port_id, flow) != 0) {
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
            int rules_nb = atoi(args.data());
            click_chatter("Generating %d 5-tuple rules\n", rules_nb);

            int i = 0;
            Vector<String> rules = rule_list_generate(rules_nb);
            for (String rule : rules) {
                Vector<String> words = rule.split(' ');
                if (words.size() != 5) {
                    input = "Usage: tcp|udp ip_src port_src ip_dst port_dst";
                    return -1;
                }
                if (words[0] == "tcp") {

                } else if (words[0] == "udp") {
                    input = "Only TCP supported for now";
                    return -1;
                } else {
                    input = "Protocol must be tcp or udp";
                    return -1;
                }

                Vector<rte_flow_item> pattern = parse_5t(words);
                struct rte_flow *flow = flow_generate(port_id, pattern);
                if (!flow) {
                    input = "Failed to insert rule " + String(i);
                    return -1;
                }

                if (fr->store_inserted_rule(port_id, flow) != 0) {
                    input = "Failed to insert rule " + String(i);
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
                input = "The argument must be 'from <group> to <another-group>'";
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
    set_handler("flow_create_5t", Handler::f_read | Handler::f_read_param, flow_handler, h_flow_create_5t);
    set_handler("flow_update_5t", Handler::f_read | Handler::f_read_param, flow_handler, h_flow_update_5t);
    set_handler("flow_jump", Handler::f_read | Handler::f_read_param, flow_handler, h_flow_jump);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel dpdk Json)

EXPORT_ELEMENT(FlowRuleInstaller)
ELEMENT_MT_SAFE(FlowRuleInstaller)
