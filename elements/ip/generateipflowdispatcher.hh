#ifndef CLICK_GENERATEIPFLOWDISPATCHER_HH
#define CLICK_GENERATEIPFLOWDISPATCHER_HH

#include <click/hashmap.hh>
#include <click/timestamp.hh>

#include "generateipfilter.hh"

CLICK_DECLS

/*
=c

GenerateIPFlowDispatcher(PORT, NB_QUEUES, NB_RULES [, POLICY, KEEP_SPORT, KEEP_DPORT] )

=s ip

generates DPDK Flow Dispatcher patterns out of input traffic

=d

Expects IP packets as input. Should be placed downstream of a
CheckIPHeader or equivalent element.


Keyword arguments are:

=6

=item PORT

Integer. Port number to install the generated rules.
Default is 0.

=item NB_QUEUES

Integer. Number of hardware queues to redirect the different rules.
Default is 16.

=item NB_RULES

Integer. Upper limit of rules to be generated.
Default is 8000.

=item POLICY

String. Determines the policy to distribute the rules across the NIC queues.
Supported policies are LOAD_AWARE and ROUND_ROBIN.
Default is LOAD_AWARE.

=item KEEP_SPORT

Boolean. Encodes the source port value of each packet into the rule.
Default is false.

=item KEEP_DPORT

Boolean. Encodes the destination port value of each packet into the rule.
Default is false.

=back

=a DPDKDevice, GenerateIPFilter */

/**
 * Uses the base class to generate Flow Dispatcher patterns out of the traffic.
 */
class GenerateIPFlowDispatcher : public GenerateIPFilter {

    public:

        GenerateIPFlowDispatcher() CLICK_COLD;
        virtual ~GenerateIPFlowDispatcher() CLICK_COLD;

        const char *class_name() const { return "GenerateIPFlowDispatcher"; }
        const char *port_count() const { return PORTS_1_1; }

        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
        int initialize(ErrorHandler *) CLICK_COLD;
        void cleanup(CleanupStage) CLICK_COLD;
        void add_handlers() CLICK_COLD;

        static String read_handler(Element *handler, void *user_data);
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
        uint16_t _nb_queues;
        /**
         * Load per NIC queue.
         */
        HashMap<uint16_t, uint64_t> _queue_load_map;

        /**
         * Rule generation logic.
         */
        enum QueueAllocPolicy {
            ROUND_ROBIN,
            LOAD_AWARE
        };
        QueueAllocPolicy _queue_alloc_policy;

        /**
         * Load imbalance per NIC queue.
         */
        enum {
            NO_LOAD = -1
        };
        HashMap<uint16_t, double> _queue_load_imbalance;

        /**
         * Quantifies the average load imbalance across all queues.
         */
        double _avg_total_load_imbalance_ratio;

        /**
         * Allocate a load counter per NIC queue,
         * according to the number of NIC queues
         * requested by the user.
         */
        void init_queue_load_map(uint16_t queues_nb);

        /**
         * Dumps rules to stdout (called by read handler dump).
         */
        virtual String dump_rules(bool verbose = false) override;

        /**
         * Dumps load per queue to stdout (called by read handler load).
         */
        String dump_load();

        /**
         * Dumps load statistics to stdout (called by read handler stats).
         */
        String dump_stats();

        /**
         * Assign rules to NIC queues according to a policy.
         */
        String policy_based_rule_generation(const uint8_t aggregation_prefix);

        /**
         * Additional handlers.
         */
        enum {
            h_load = 2,
            h_stats,
            h_avg_imbalance_ratio,
            h_queue_imbalance_ratio
        };

};

CLICK_ENDDECLS

#endif
