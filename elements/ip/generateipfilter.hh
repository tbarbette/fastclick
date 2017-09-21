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

Integer. Number of rules to be generated.
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
        virtual ~GenerateIPPacket() CLICK_COLD;

        const char *class_name() const { return "GenerateIPPacket"; }
        const char *port_count() const { return PORTS_1_1; }

        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
        int initialize(ErrorHandler *) CLICK_COLD;
        void cleanup(CleanupStage) CLICK_COLD;

        // Make this base class abstract
        virtual Packet *simple_action(Packet *p) = 0;
    #if HAVE_BATCH
        virtual PacketBatch *simple_action_batch(PacketBatch *batch) = 0;
    #endif

    protected:

        class IPFlow {

            public:

                typedef IPFlowID key_type;
                typedef const IPFlowID &key_const_reference;

                IPFlow() {};

                void initialize(const IPFlowID &flowid) {
                    _flowid = flowid;
                }

                const IPFlowID &flowid() const {
                    return _flowid;
                }


                void setMask(IPFlowID mask) {
                    _flowid = _flowid & mask;
                }

                key_const_reference hashkey() const {
                    return _flowid;
                }

            private:

                IPFlowID _flowid;
        };

        int _nrules;
        HashTable<IPFlow> _map;
        IPFlowID _mask;

        static const int DEF_NB_RULES;

};

/**
 * Uses the base class to generate IPFilter patterns out of the traffic.
 */
class GenerateIPFilter : public GenerateIPPacket {

    public:

        GenerateIPFilter() CLICK_COLD;
        ~GenerateIPFilter() CLICK_COLD;

        const char *class_name() const { return "GenerateIPFilter"; }
        const char *port_count() const { return PORTS_1_1; }

        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
        int initialize(ErrorHandler *) CLICK_COLD;
        void cleanup(CleanupStage) CLICK_COLD;
        void add_handlers() CLICK_COLD;

        static String read_handler(Element *handler, void *user_data);

        Packet *simple_action(Packet *p);
    #if HAVE_BATCH
        PacketBatch *simple_action_batch(PacketBatch *batch);
    #endif

    protected:

        bool _keep_sport;
        bool _keep_dport;

};

CLICK_ENDDECLS
#endif
