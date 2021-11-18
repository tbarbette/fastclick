// -*- mode: c++; c-basic-offset: 4 -*-

#ifndef CLICK_FLOW_RULE_INSTALLER_HH
#define CLICK_FLOW_RULE_INSTALLER_HH

#include <click/element.hh>
#include <click/task.hh>
#include <click/notifier.hh>
#include <click/hashmap.hh>
#include <click/dpdkdevice.hh>
#include <click/handlercall.hh>

#include "../userlevel/fromdpdkdevice.hh"

#include <rte_flow.h>

class FlowRuleInstaller : public Element {
    public:
        FlowRuleInstaller() CLICK_COLD;
        ~FlowRuleInstaller() CLICK_COLD;

        const char *class_name() const  { return "FlowRuleInstaller"; }
        const char *port_count() const  { return PORTS_0_0; }

        int configure_phase() const { return CONFIGURE_PHASE_LAST; }

        int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
        int initialize(ErrorHandler *) override CLICK_COLD;
        void cleanup(CleanupStage) override CLICK_COLD;
        static int static_cleanup();

        void add_handlers() override CLICK_COLD;

    private:
        int _core_id;
        int _group;

        FromDPDKDevice *_fd;
        Vector<struct rte_flow *> _rules;

        /* Verbose */
        bool _verbose;

        Timer _timer;

        int store_rule_local(struct rte_flow *rule);
        int remove_rule_local(struct rte_flow *rule);
        String get_nic_name_from_handler_input(String &input);

        static Vector<rte_flow_item> parse_5t(Vector<String> words,bool is_tcp = true, bool have_ports = true);
        static struct rte_flow *flow_add_redirect(portid_t port_id, int from, int to, bool validate, int priority = 0);
        static Vector<String> rule_list_generate(const int &rules_nb, bool with_ports = true, String mask = "255.255.255.255", String proto = "udp");
        static struct rte_flow *flow_generate(portid_t port_id, Vector<rte_flow_item> &pattern, int table, int priority = 0, int index = 1, struct rte_flow* = 0);

        static int flow_handler(int operation, String &input, Element *e, const Handler *handler, ErrorHandler *errh);
};

CLICK_ENDDECLS

#endif
