#ifndef CLICK_GENERATEIPFILTER_HH
#define CLICK_GENERATEIPFILTER_HH
#include <click/batchelement.hh>
#include <click/ipflowid.hh>
#include <click/hashmap.hh>
#include <click/hashtable.hh>

CLICK_DECLS

/*
=c

GenerateIPFilter(NB_RULES [, KEEP_SADDR, KEEP_DADDR, KEEP_SPORT, KEEP_DPORT, PREFIX, PATTERN_TYPE, OUT_FILE])

=s ip

generates IPFilter patterns out of input traffic

=d

Expects IP packets as input. Should be placed downstream of a
CheckIPHeader or equivalent element.

This element supports rules for the IPFilter and IPClassifier Click elements.
However, it also acts as a parent class for other similar elements, such as:
 * GenerateIPLookup (for IPLookup Click elements)
 * GenerateIPFlowDispatcher (for rules following DPDK's Flow API)
 * GenerateOpenFlow (for OpenFlow rules targeting OVS or ONOS's northbound API)

Keyword arguments are:

=8

=item NB_RULES

Integer. Upper limit of rules to be generated.
Default is 1000.

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

=item PATTERN_TYPE

String. Defines the format of the generated rules.
The list of supported formats follows:
 * IPFILTER
 * IPCLASSIFIER
Default is IPFILTER.

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

=a IPFilter, GenerateIPLookup, GenerateIPFlowDispatcher, GenerateOpenFlow */

enum RuleFormat {
    RULE_DPDK = 0,
    RULE_IPCLASSIFIER,
    RULE_IPFILTER,
    RULE_IPLOOKUP,
    RULE_OF_ONOS,
    RULE_OF_OVS,
};

/**
 * Abstract, base class that offers IPFlow representation & storage.
 */
class GenerateIPPacket : public BatchElement {

    public:
        GenerateIPPacket() CLICK_COLD;
        ~GenerateIPPacket() CLICK_COLD;

        const char *class_name() const { return "GenerateIPPacket"; }
        const char *port_count() const { return PORTS_1_1; }

        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
        int initialize(ErrorHandler *) CLICK_COLD;
        void cleanup(CleanupStage) CLICK_COLD;

        // Make this base class abstract
        virtual Packet *simple_action(Packet *p) override = 0;
    #if HAVE_BATCH
        virtual PacketBatch *simple_action_batch(PacketBatch *batch) override = 0;
    #endif

        class IPFlow {

            public:
                typedef IPFlowID key_type;
                typedef const IPFlowID &key_const_reference;

                IPFlow() {};

                void initialize(const IPFlowID &flowid) {
                    _flowid = flowid;
                    _flow_proto = 0;
                    _flow_size_bytes = 0;
                }

                const IPFlowID &flowid() const {
                    return _flowid;
                }

                uint8_t flow_proto() {
                    return _flow_proto;
                }

                uint64_t flow_size() {
                    return _flow_size_bytes;
                }

                void set_mask(IPFlowID mask) {
                    _flowid = _flowid & mask;
                }

                void set_proto(const uint8_t proto) {
                    _flow_proto += proto;
                }

                void set_flow_size(const uint64_t flow_size) {
                    _flow_size_bytes = flow_size;
                }

                void update_flow_size(const uint64_t extra_size) {
                    _flow_size_bytes += extra_size;
                }

                key_const_reference hashkey() const {
                    return _flowid;
                }

                void print_flow_info() {
                    click_chatter("Flow with ID: %s - Proto: %d - Size: %" PRIu64 " bytes",
                        _flowid.unparse().c_str(), _flow_proto, _flow_size_bytes);
                }

            private:
                IPFlowID _flowid;
                uint8_t  _flow_proto;
                uint64_t _flow_size_bytes;
        };

    protected:
        int build_mask(IPFlowID &mask, bool keep_saddr, bool keep_daddr, bool keep_sport, bool keep_dport, int prefix);
        int flows_nb() { return _map.size(); };

        int _nrules;
        uint64_t _flows_nb;
        int _inst_rules;
        HashTable<IPFlow> _map;
        IPFlowID _mask;
        int _prefix;

        static const int DEF_NB_RULES;
        static const uint16_t INCLUDE_TP_PORT;

        // Deriving classes must be able to dump rules
        virtual IPFlowID get_mask(int prefix) = 0;
        virtual bool is_wildcard(const IPFlow &flow) = 0;
        virtual int prepare_rules(bool verbose = false) = 0;
        virtual int count_rules() = 0;
        virtual String dump_rules(const RuleFormat& rf, bool verbose = false) = 0;
        virtual int dump_rules_to_file(const String &content) = 0;

};

class RuleFormatter;

/**
 * Uses the base class to generate IPFilter patterns out of the traffic.
 */
class GenerateIPFilter : public GenerateIPPacket {

    public:
        /**
         * Rule pattern type.
         */
        enum RulePattern {
            IPFILTER,
            IPCLASSIFIER,
            IPLOOKUP,
            FLOW_DISPATCHER,
            OPENFLOW,
            NONE
        };

        GenerateIPFilter() CLICK_COLD;
        GenerateIPFilter(RulePattern pattern_type) CLICK_COLD;
        ~GenerateIPFilter() CLICK_COLD;

        const char *class_name() const { return "GenerateIPFilter"; }
        const char *port_count() const { return PORTS_1_1; }

        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
        int initialize(ErrorHandler *) CLICK_COLD;
        void cleanup(CleanupStage) CLICK_COLD;
        void add_handlers() CLICK_COLD;

        virtual Packet *simple_action(Packet *p) override;
    #if HAVE_BATCH
        virtual PacketBatch *simple_action_batch(PacketBatch *batch) override;
    #endif

        static String read_handler(Element *handler, void *user_data);
        static String to_file_handler(Element *handler, void *user_data);

        /**
         * Maps rule format to the respective handler.
         */
        static HashMap<uint8_t, RuleFormatter *> _rule_formatter_map;

    protected:
        bool _keep_saddr;
        bool _keep_daddr;
        bool _keep_sport;
        bool _keep_dport;
        bool _map_reduced;
        int _pref_reduced;

        RulePattern _pattern_type;

        String _out_file;

        /**
         * Handlers.
         */
        enum {
            h_flows_nb = 0,
            h_rules_nb,
            h_dump,
            h_dump_to_file,
        };

        IPFlowID get_mask(int prefix) override;
        bool is_wildcard(const IPFlow &flow) override;
        int prepare_rules(bool verbose = false) override;
        int count_rules() override;

        virtual String dump_rules(const RuleFormat& rf, bool verbose = false) override;
        int dump_rules_to_file(const String &content) override;

};

/**
 * Rule formatter abstract class.
 */
class RuleFormatter {
    public:
        RuleFormatter(bool s_port, bool d_port) : _with_tp_s_port(s_port), _with_tp_d_port(d_port) {};
        ~RuleFormatter() {};

        virtual String flow_to_string(GenerateIPPacket::IPFlow &flow, const uint32_t flow_nb, const uint8_t prefix) = 0;

    protected:
        /**
         * The generated rules can conditionally match transport ports.
         */
        bool _with_tp_s_port;
        bool _with_tp_d_port;
};

/**
 * IPClassifier rule formatter.
 */
class IPClassifierRuleFormatter : public RuleFormatter {
    public:
        IPClassifierRuleFormatter(bool s_port, bool d_port) : RuleFormatter(s_port, d_port) {};
        ~IPClassifierRuleFormatter() {};

        virtual String flow_to_string(GenerateIPPacket::IPFlow &flow, const uint32_t flow_nb, const uint8_t prefix) override;
};

/**
 * IPFilter rule formatter.
 */
class IPFilterRuleFormatter : public IPClassifierRuleFormatter {
    public:
        IPFilterRuleFormatter(bool s_port, bool d_port) : IPClassifierRuleFormatter(s_port, d_port) {};
        ~IPFilterRuleFormatter() {};

        virtual String flow_to_string(GenerateIPPacket::IPFlow &flow, const uint32_t flow_nb, const uint8_t prefix) override;
};

CLICK_ENDDECLS
#endif
