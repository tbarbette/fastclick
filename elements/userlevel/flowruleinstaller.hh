// -*- mode: c++; c-basic-offset: 4 -*-

#ifndef CLICK_FLOW_RULE_INSTALLER_HH
#define CLICK_FLOW_RULE_INSTALLER_HH

#include <click/element.hh>
#include <click/task.hh>
#include <click/notifier.hh>
#include <click/hashmap.hh>
#include <click/dpdkdevice.hh>
#include <click/handlercall.hh>

#include "fromdpdkdevice.hh"

#include <rte_flow.h>

class NIC {
    public:
        NIC *mirror;

        NIC(bool verbose = false);
        NIC(const NIC &n);
        ~NIC();

        inline Element *get_element() { return _element; };
        inline bool is_ghost() { return (get_element() == NULL); };
        inline int get_index() { return is_ghost() ? -1 : _index; };
        inline portid_t get_port_id() { return is_ghost() ? -1 : cast()->get_device()->get_port_id(); };
        inline String get_name() { return is_ghost() ? "" : _element->name(); };
        inline String get_device_address() { return String(get_port_id()); };
        inline bool has_mirror() { return mirror && (this->_index != mirror->_index); };

        void set_element(Element *el);
        void set_index(const int &index);

        FromDPDKDevice *cast();

        int queue_per_pool();
        int phys_cpu_to_queue(int phys_cpu_id);

        String call_rx_read(String h);
        String call_tx_read(String h);
        int    call_rx_write(String h, const String input);

    private:
        Element *_element; // Click element associated with this NIC
        int _index;        // Click port index of this NIC
        bool _verbose;     // Verbosity flag
};

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

        void run_timer(Timer *t) override;

        void add_handlers() override CLICK_COLD;

        NIC *get_nic_by_index(int i) {
            auto it = _nics.begin();
            while (it != _nics.end()) {
                NIC *nic = &it.value();
                if (nic->get_port_id() == (portid_t) i) {
                    return nic;
                }
                it++;
            }
            return NULL;
        }

        NIC *get_nic_by_name(String name) {
            auto it = _nics.begin();
            while (it != _nics.end()) {
                NIC *nic = &it.value();
                if (nic->get_name() == name) {
                    return nic;
                }
                it++;
            }
            return NULL;
        }

    private:
        int _group;
        int _core_id;
        unsigned _timer_period;

        HashMap<String, NIC> _nics;
        HashMap<portid_t, Vector<struct rte_flow *>> _rules;

        bool _verbose;

        Timer _timer;

        int store_inserted_rule(portid_t port_id, struct rte_flow *rule);
        String get_nic_name_from_handler_input(String &input);
        FromDPDKDevice *get_nic_device_from_name(String &nic_name);

        static Vector<rte_flow_item> parse_5t(Vector<String> words,bool is_tcp = true, bool have_ports = true);
        static struct rte_flow *flow_add_redirect(int port_id, int from, int to, bool validate, int priority = 0);
        static Vector<String> rule_list_generate(const int &rules_nb);
        static struct rte_flow *flow_generate(portid_t port_id, int group, Vector<rte_flow_item> &pattern);

        static int flow_handler(int operation, String &input, Element *e, const Handler *handler, ErrorHandler *errh);
};

CLICK_ENDDECLS

#endif
