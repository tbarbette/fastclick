#ifndef CLICK_GENERATEIPFLOWDIRECTOR_HH
#define CLICK_GENERATEIPFLOWDIRECTOR_HH
#include "generateipfilter.hh"
#include "generateipflowdirector.hh"

CLICK_DECLS

/*
=c

GenerateIPFlowDirector(NB_RULES, NB_CORES)

=s ip

generates Flow Director patterns out of input traffic

=d

Expects IP packets as input. Should be placed downstream of a
CheckIPHeader or equivalent element.


Keyword arguments are:

=2

=item NB_RULES

Integer. Number of rules to be generated.
Default is 8000.

=item NB_CORES

Integer. Number of cores to redirect the different rules from the NIC.
Default is 16.

=back

=a DPDKDevice, GenerateIPFilter */

/**
 * Base class that offers IPFlow representation & storage.
 */
class GenerateIPFilter;

/**
 * Uses the base class to generate Flow Director patterns out of the traffic.
 */
class GenerateIPFlowDirector : public GenerateIPFilter {

    public:

        GenerateIPFlowDirector() CLICK_COLD;
        ~GenerateIPFlowDirector() CLICK_COLD;

        const char *class_name() const { return "GenerateIPFlowDirector"; }
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
         * When the dump handler is called, each rule is assigned
         * to a CPU core in a round-robin fashion.
         */
        static int _nb_cores;
        static const int DEF_NB_CORES;

};

CLICK_ENDDECLS

#endif
