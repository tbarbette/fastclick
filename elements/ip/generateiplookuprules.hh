#ifndef CLICK_GENERATEIPLOOKUP_HH
#define CLICK_GENERATEIPLOOKUP_HH
#include "generateipfilterrules.hh"

CLICK_DECLS

/*
=c

GenerateIPLookupRules(NB_RULES, OUT_PORT, [, PREFIX, OUT_FILE])

=s ip

generates IPRouteTable rule patterns out of input traffic

=d

Expects IP packets as input. Should be placed downstream of a
CheckIPHeader or equivalent element.


Keyword arguments are:

=4

=item NB_RULES

Integer. Number of rules to be generated.
Default is 1000.

=item OUT_PORT

Integer. Output port where the generated routing entries will be sent to.
Default is 1.

=item KEEP_SADDR

Boolean. Encodes the source IP address of each packet into the rule.
Setting this argument to true has not effect as the routing is solely based on the destination IP address.
Hardcoded to false.

=item KEEP_DADDR

Boolean. Encodes the destination IP address of each packet into the rule.
This argument is always true for this element.

=item KEEP_SPORT

Boolean. Encodes the source port value of each packet into the rule.
Setting this argument to true has not effect as the routing is solely based on the destination IP address.
Hardcoded to false.

=item KEEP_DPORT

Boolean. Encodes the destination port value of each packet into the rule.
Setting this argument to true has not effect as the routing is solely based on the destination IP address.
Hardcoded to false.

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

=back

=a IPRouteTable, LookupIPRouteMP, GenerateIPFilterRules */

/**
 * Abstract, base class that offers IPFlow representation & storage.
 */
class GenerateIPFilterRules;

/**
 * Uses the base class to generate IPRouteTable patterns out of the traffic.
 */
class GenerateIPLookupRules : public GenerateIPFilterRules {

    public:
        GenerateIPLookupRules() CLICK_COLD;
        ~GenerateIPLookupRules() CLICK_COLD;

        const char *class_name() const { return "GenerateIPLookupRules"; }
        const char *port_count() const { return PORTS_1_1; }

        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
        int initialize(ErrorHandler *) CLICK_COLD;
        void cleanup(CleanupStage) CLICK_COLD;
        void add_handlers() CLICK_COLD;

        static String read_handler(Element *handler, void *user_data);
        static String to_file_handler(Element *handler, void *user_data);

    private:
        /**
         * Element's output port associated with the rules.
         */
        uint8_t _out_port;

        IPFlowID get_mask(int prefix) override;
        bool is_wildcard(const IPFlow &flow) override;

};

/**
 * IPLookup rule formatter class.
 */
class IPLookupRuleFormatter : public RuleFormatter {
    public:
        IPLookupRuleFormatter(uint8_t out_p, bool s_port, bool d_port) :
            RuleFormatter(s_port, d_port), _out_port(out_p) {};
        ~IPLookupRuleFormatter() {};

        virtual String flow_to_string(GenerateIPPacketRules::IPFlow &flow, const uint32_t flow_nb, const uint8_t prefix);

    protected:
        /**
         * IPLookup element's output port associated with the rules.
         */
        uint8_t _out_port;
};

CLICK_ENDDECLS
#endif
