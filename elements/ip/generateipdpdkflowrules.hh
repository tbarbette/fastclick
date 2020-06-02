#ifndef CLICK_GENERATEIPDPDKFLOWRULES_HH
#define CLICK_GENERATEIPDPDKFLOWRULES_HH

#include <click/hashmap.hh>
#include <click/timestamp.hh>

#include "generateipfilterrules.hh"

CLICK_DECLS

/*
=c

GenerateIPDPDKFlowRules(PORT, NB_QUEUES, NB_RULES [, POLICY, KEEP_SADDR, KEEP_DADDR, KEEP_SPORT, KEEP_DPORT, PREFIX, OUT_FILE])

=s ip

generates DPDK Flow API rule patterns out of input traffic

=d

Expects IP packets as input. Should be placed downstream of a
CheckIPHeader or equivalent element.

Keyword arguments are:

=10

=item PORT

Integer. DPDK port number to install the generated rules.
Default is 0.

=item NB_QUEUES

Integer. Number of hardware queues to redirect the different rules.
Default is 16.

=item NB_RULES

Integer. Upper limit of rules to be generated.
Default is 1000.

=item POLICY

String. Determines the policy to distribute the rules across the NIC queues.
Supported policies are LOAD_AWARE and ROUND_ROBIN.
Default is LOAD_AWARE.

=item KEEP_SADDR

Boolean. Encodes the source IP address of each packet into the rule.
Default is true.

=item KEEP_DADDR

Boolean. Encodes the destination IP address of each packet into the rule.
Default is true.

=item KEEP_SPORT

Boolean. Encodes the source port value of each packet into the rule.
Default is false.

=item KEEP_DPORT

Boolean. Encodes the destination port value of each packet into the rule.
Default is false.

=item PREFIX

Integer. A prefix number to perform subnetting.
Default is 32.

=item OUT_FILE

String. A filename to ouput the generated rules.
For this argument to work, you need to call the dump_to_file handler.

=h rules_nb read-only

Outputs the number of rules being generated.

=h dump read-only

Outputs the rules to stdout.

=h dump_to_file read-only

Outputs the rules to a designated file.

=h load read-only

Outputs the load per queue.

=h stats read-only

Outputs per-queue statistics related to the ideal vs. observed load.

=h avg_imbalance_ratio read-only

Outputs the load imbalance ratio across queues.

=h queue_imbalance_ratio read-only

Outputs the load imbalance ratio of a certain queue.

=back

=a DPDKDevice, FlowDispatcher, GenerateIPFilterRules */

/**
 * Uses the base class to generate DPDK flow rule patterns out of the traffic.
 */
class GenerateIPDPDKFlowRules : public GenerateIPFilterRules {

    public:

        GenerateIPDPDKFlowRules() CLICK_COLD;
        virtual ~GenerateIPDPDKFlowRules() CLICK_COLD;

        const char *class_name() const { return "GenerateIPDPDKFlowRules"; }
        const char *port_count() const { return PORTS_1_1; }

        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
        int initialize(ErrorHandler *) CLICK_COLD;
        void cleanup(CleanupStage) CLICK_COLD;
        void add_handlers() CLICK_COLD;

        static String read_handler(Element *handler, void *user_data);
        static String to_file_handler(Element *handler, void *user_data);
        static int param_handler(
            int operation, String &param, Element *e,
            const Handler *, ErrorHandler *errh
        ) CLICK_COLD;

    private:
        /**
         * NIC to host the rules.
         */
        uint16_t _port;

        /**
         * Number of NIC hardware queues to be used for load balancing.
         */
        uint16_t _queues_nb;

        /**
         * Returns the mask of this element.
         */
        IPFlowID get_mask(int prefix) override;

        /**
         * Returns whether a flow is a wildcard or not.
         */
        bool is_wildcard(const IPFlow &flow) override;

        /**
         * Additional handlers.
         */
        enum {
            h_load = 10,
            h_stats,
            h_avg_imbalance_ratio,
            h_queue_imbalance_ratio
        };

};

/**
 * Rule generation logic.
 */
enum QueueAllocPolicy {
    LOAD_AWARE,
    ROUND_ROBIN,
};

/**
 * Indicates no load on a queue.
 */
enum {
    NO_LOAD = -1
};

/**
 * NIC queue load store.
 */
class QueueLoad {
    public:
        QueueLoad(uint16_t queues_nb, QueueAllocPolicy queue_policy) :
            _queues_nb(queues_nb),
            _queue_alloc_policy(queue_policy),
            _queue_load_map(),
            _queue_load_imbalance(),
            _avg_total_load_imbalance_ratio(NO_LOAD) {
            assert(_queues_nb > 0);
            for (uint16_t i = 0; i < _queues_nb; i++) {
                _queue_load_map.insert(i, 0);
                _queue_load_imbalance.insert(i, NO_LOAD);
            }
        };
        ~QueueLoad() {
            if (!_queue_load_map.empty()) {
                _queue_load_map.clear();
            }

            if (!_queue_load_imbalance.empty()) {
                _queue_load_imbalance.clear();
            }
        }

        /**
         * Dumps load per queue to stdout (called by read handler load).
         */
        String dump_load();

        /**
         * Dumps load statistics to stdout (called by read handler stats).
         */
        String dump_stats();

        inline double get_load_imbalance_of_queue(const uint16_t &queue_id) {
            if (valid_queue_id(queue_id)) {
                return _queue_load_imbalance[queue_id];
            }
            return (double) -1;
        }

        inline double get_avg_load_imbalance() {
            return _avg_total_load_imbalance_ratio;
        }

        inline bool imbalance_not_computed() {
            return _avg_total_load_imbalance_ratio == NO_LOAD;
        }

        inline bool valid_queue_id(const uint16_t &queue_id) {
            if ((queue_id < 0) || (queue_id >= _queues_nb)) {
                return false;
            }
            return true;
        }

        /**
         * Number of NIC hardware queues.
         */
        uint16_t _queues_nb;

        /**
         * Policy to associate rules to hardware queues.
         */
        QueueAllocPolicy _queue_alloc_policy;

        /**
         * Load per NIC queue.
         */
        HashMap<uint16_t, uint64_t> _queue_load_map;

        /**
         * Load imbalance per NIC queue.
         */
        HashMap<uint16_t, double> _queue_load_imbalance;

        /**
         * Quantifies the average load imbalance across all queues.
         */
        double _avg_total_load_imbalance_ratio;
};

/**
 * DPDK flow rule formatter class.
 */
class DPDKFlowRuleFormatter : public RuleFormatter {
    public:
        DPDKFlowRuleFormatter(uint16_t port, uint16_t queues_nb, QueueAllocPolicy queue_policy, bool s_port, bool d_port) :
            RuleFormatter(s_port, d_port),
            _port(port), _queues_nb(queues_nb), _queue_load(0) {
            _queue_load = new QueueLoad(queues_nb, queue_policy);
            assert(_queue_load);
        }
        ~DPDKFlowRuleFormatter() {
            if (_queue_load) {
                delete _queue_load;
            }
        }

        inline QueueLoad *get_queue_load() { return _queue_load; };

        virtual String flow_to_string(GenerateIPPacketRules::IPFlow &flow, const uint32_t flow_nb, const uint8_t prefix);

    protected:
        /**
         * NIC port ID associated with the rules.
         */
        uint16_t _port;

        /**
         * Number of NIC hardware queues to be used for load balancing.
         */
        uint16_t _queues_nb;

    private:
        /**
         * Stores load information per NIC queue.
         */
        QueueLoad *_queue_load;

        /**
         * Assign a NIC queue to a rule according to a policy.
         */
        String policy_based_rule_generation(GenerateIPPacketRules::IPFlow &flow, const uint32_t flow_nb, const uint8_t prefix);
};

CLICK_ENDDECLS

#endif
