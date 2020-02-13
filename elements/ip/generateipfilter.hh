#ifndef CLICK_GENERATEIPFILTER_HH
#define CLICK_GENERATEIPFILTER_HH
#include <click/batchelement.hh>
#include <click/ipflowid.hh>
#include <click/hashtable.hh>

CLICK_DECLS

/*
=c

GenerateIPFilter(NB_RULES [, KEEP_SPORT, KEEP_DPORT])

=s ip

generates IPFilter patterns out of input traffic

=d

Expects IP packets as input. Should be placed downstream of a
CheckIPHeader or equivalent element.


Keyword arguments are:

=3

=item NB_RULES

Integer. Upper limit of rules to be generated.
Default is 8000.

=item KEEP_SPORT

Boolean. Encodes the source port value of each packet into the rule.
Default is false.

=item KEEP_DPORT

Boolean. Encodes the destination port value of each packet into the rule.
Default is true.

=back

=a IPFilter, GenerateIPLookup, Print */

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

    protected:

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

            private:

                IPFlowID _flowid;
                uint8_t  _flow_proto;
                uint64_t _flow_size_bytes;
        };

        int build_mask(IPFlowID &mask, bool keep_saddr, bool keep_daddr, bool keep_sport, bool keep_dport, int prefix);

        int _nrules;
        HashTable<IPFlow> _map;
        IPFlowID _mask;
        int _prefix;

        static const int DEF_NB_RULES;

        // Deriving classes must be able to dump rules
        virtual String dump_rules(bool verbose = false) = 0;

};

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

    protected:

        bool _keep_saddr;
        bool _keep_daddr;
        bool _keep_sport;
        bool _keep_dport;

        RulePattern _pattern_type;

        /**
         * Handlers.
         */
        enum {
            h_dump = 0,
            h_rules_nb
        };

        virtual String dump_rules(bool verbose = false) override;

};

CLICK_ENDDECLS
#endif
