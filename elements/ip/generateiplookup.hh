#ifndef CLICK_GENERATEIPLOOKUP_HH
#define CLICK_GENERATEIPLOOKUP_HH
#include "generateipfilter.hh"

CLICK_DECLS

/*
=c

GenerateIPLookup(NB_RULES, OUT_PORT)

=s ip

generates IPRouteTable patterns out of input traffic

=d

Expects IP packets as input. Should be placed downstream of a
CheckIPHeader or equivalent element.


Keyword arguments are:

=2

=item NB_RULES

Integer. Number of rules to be generated.
Default is 8000.

=item OUT_PORT

Integer. Output port where the generated routing entries will be sent to.
Default is 1.

=back

=a IPRouteTable, LookupIPRouteMP, GenerateIPFilter */

/**
 * Abstract, base class that offers IPFlow representation & storage.
 */
class GenerateIPPacket;

/**
 * Uses the base class to generate IPRouteTable patterns out of the traffic.
 */
class GenerateIPLookup : public GenerateIPPacket {

    public:

        GenerateIPLookup() CLICK_COLD;
        ~GenerateIPLookup() CLICK_COLD;

        const char *class_name() const { return "GenerateIPLookup"; }
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

    private:

        /**
         * Element's output port associated with the rules.
         */
        uint8_t _out_port;

};

CLICK_ENDDECLS
#endif
