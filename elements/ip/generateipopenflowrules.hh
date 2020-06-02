#ifndef CLICK_GENERATEIPOPENFLOWRULES_HH
#define CLICK_GENERATEIPOPENFLOWRULES_HH
#include "generateipfilterrules.hh"

CLICK_DECLS

/*
=c

GenerateIPOpenFlowRules(OF_PROTO, OF_BRIDGE, OF_TABLE, IN_PORT, OUT_PORT, NB_RULES, [, KEEP_SADDR, KEEP_DADDR, KEEP_SPORT, KEEP_DPORT, PREFIX, OUT_FILE])

=s ip

generates OpenFlow rules out of input traffic

=d

Expects IP packets as input. Should be placed downstream of a
CheckIPHeader or equivalent element.
The generated rules target:
  * a specific system, such as OVS or ONOS (use the correct handler to convert to the desired format)
  * a given OpenFlow version (see OF_PROTO argument)
  * a given OpenFlow bridge name (see OF_BRIDGE argument)
  * a given OpenFlow table number (see OF_TABLE argument)
  * a given input port on the selected bridge (see IN_PORT)
  * a given output port on the selected bridge (see OUT_PORT)

The match operations are based upon the incoming packets' 5-tuple if KEEP_SADDR, KEEP_DADDR, KEEP_SPORT, KEEP_DPORT
and true. Fewer matches occur if some of these arguments are set to false.
The actions are currently restricted to output port dispatching.

Keyword arguments are:

=12

=item OF_PROTO

Integer. OpenFlow protocol version.
Setting this argument to 3, corresponds to OpenFlow1.3.
No default value, this argument is mandatory.

=item OF_BRIDGE

String. OpenFlow bridge name.
No default value, this argument is mandatory.

=item OF_TABLE

Integer. OpenFlow table number.
Default to 0, i.e., the first table in the pipeline.

=item IN_PORT

Integer. Input port number of the switch to receive the traffic.
Default is 1.

=item OUT_PORT

Integer. Output port number of the switch to emit the matching traffic.
Default is 1.

=item NB_RULES

Integer. Number of rules to be generated.
Default is 100.

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
Default is true.

=item PREFIX

Integer. A prefix number to perform subnetting.
Default is 32.

=item OUT_FILE

String. A filename to ouput the generated rules.
For this argument to work, you need to call the dump_to_file handler.

=h rules_nb read-only

Outputs the number of rules being generated.

=h dump read-only

Outputs the rules to stdout. Follows the OVS format, hence it can be used interchangeably with dump_ovs.

=h dump_ovs read-only

Outputs the rules following the OVS format to stdout.

=h dump_onos read-only

Outputs the rules following the ONOS format to stdout.

=h dump_to_file read-only

Outputs the rules to a designated file. Follows the OVS format, hence it can be used interchangeably with dump_ovs_to_file.

=h dump_ovs_to_file read-only

Outputs the rules following the OVS format to a designated file.

=h dump_onos_to_file read-only

Outputs the rules following the ONOS format to a designated file.

=back

=a GenerateIPFilterRules */

/**
 * Abstract, base class that offers IPFlow representation & storage.
 */
class GenerateIPFilterRules;
class RuleFormatter;

enum OpenFlowProtoVersion {
    OF_PROTO_1_0 = 0,
    OF_PROTO_1_1,
    OF_PROTO_1_2,
    OF_PROTO_1_3,
    OF_PROTO_1_4,
    OF_PROTO_1_5,
};

/**
 * Uses the base class to generate OpenFlow rules out of the traffic.
 */
class GenerateIPOpenFlowRules : public GenerateIPFilterRules {

    public:
        GenerateIPOpenFlowRules() CLICK_COLD;
        ~GenerateIPOpenFlowRules() CLICK_COLD;

        const char *class_name() const { return "GenerateIPOpenFlowRules"; }
        const char *port_count() const { return PORTS_1_1; }

        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
        int initialize(ErrorHandler *) CLICK_COLD;
        void cleanup(CleanupStage) CLICK_COLD;
        void add_handlers() CLICK_COLD;

        static String read_handler(Element *handler, void *user_data);
        static String to_file_handler(Element *handler, void *user_data);

    private:
        /**
         * Additional handlers
         */
        enum {
            h_dump_ovs = 100,
            h_dump_onos = 101,
        };

        IPFlowID get_mask(int prefix) override;
        bool is_wildcard(const IPFlow &flow) override;

        // int prepare_rules(bool verbose = false);
        String dump_ovs_rules(bool verbose = false);
        String dump_onos_rules(bool verbose = false);

};

/**
 * OpenFlow formatter abstract class.
 */
class OpenFlowRuleFormatter : public RuleFormatter {
    public:
        OpenFlowRuleFormatter(OpenFlowProtoVersion of_p_ver, String of_br,
                          uint16_t of_t, uint8_t in_p, uint8_t out_p,
                          bool s_port, bool d_port) : RuleFormatter(s_port, d_port),
            _of_proto_ver(of_p_ver), _of_bridge(of_br), _of_table(of_t),
            _in_port(in_p), _out_port(out_p) {};
        ~OpenFlowRuleFormatter() {};

        virtual String flow_to_string(GenerateIPPacketRules::IPFlow &flow, const uint32_t flow_nb, const uint8_t prefix) = 0;

    protected:
        /**
         * OpenFlow protocol version.
         */
        OpenFlowProtoVersion _of_proto_ver;

        /**
         * OpenFlow bridge to associate the generate rules with.
         */
        String _of_bridge;

        /**
         * OpenFlow table to store the generate rules.
         */
        uint16_t _of_table;

        /**
         * Switch's input port associated with the rules.
         */
        uint8_t _in_port;

        /**
         * Switch's output port associated with the rules.
         */
        uint8_t _out_port;
};

/**
 * OVS-specific OpenFlow formatter.
 */
class OVSOpenFlowRuleFormatter : public OpenFlowRuleFormatter {
    public:
        OVSOpenFlowRuleFormatter(OpenFlowProtoVersion of_p_ver, String of_br,
                                 uint16_t of_t, uint8_t in_p, uint8_t out_p,
                                 bool s_port, bool d_port) :
            OpenFlowRuleFormatter(of_p_ver, of_br, of_t, in_p, out_p, s_port, d_port) {};
        ~OVSOpenFlowRuleFormatter() {};

        virtual String flow_to_string(GenerateIPPacketRules::IPFlow &flow, const uint32_t flow_nb, const uint8_t prefix) override;
};

/**
 * ONOS-specific OpenFlow formatter.
 */
class ONOSOpenFlowRuleFormatter : public OpenFlowRuleFormatter {
    public:
        ONOSOpenFlowRuleFormatter(OpenFlowProtoVersion of_p_ver, String of_br,
                                  uint16_t of_t, uint8_t in_p, uint8_t out_p,
                                  bool s_port, bool d_port) :
            OpenFlowRuleFormatter(of_p_ver, of_br, of_t, in_p, out_p, s_port, d_port) {};
        ~ONOSOpenFlowRuleFormatter() {};

        virtual String flow_to_string(GenerateIPPacketRules::IPFlow &flow, const uint32_t flow_nb, const uint8_t prefix) override;
};

CLICK_ENDDECLS
#endif
