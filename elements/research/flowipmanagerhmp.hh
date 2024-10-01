#ifndef CLICK_FLOWIPMANAGERHMP_HH
#define CLICK_FLOWIPMANAGERHMP_HH
#include <click/config.h>
#include <click/string.hh>
#include <click/timer.hh>
#include <click/vector.hh>
#include <click/multithread.hh>
#include <click/batchelement.hh>
#include <click/hashtablemp.hh>
#include <click/pair.hh>
#include <click/ipflowid.hh>
#include <click/flow/flowelement.hh>
#include <click/flow/common.hh>
#include <click/timerwheel.hh>

#include "../research/flowipmanager.hh"

CLICK_DECLS

class DPDKDevice;

/**
 * FlowIPManager based on the HashtableMP (hierarchical locked hashtable)
 *
 * @see also FlowIPManager
 */
class FlowIPManagerHMP: public VirtualFlowManager, Router::InitFuture {

    public:
        FlowIPManagerHMP() CLICK_COLD;
        ~FlowIPManagerHMP() CLICK_COLD;

        const char *class_name() const override { return "FlowIPManagerHMP"; }
        const char *port_count() const override { return "1/1"; }

        int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
        int initialize(ErrorHandler *errh) override CLICK_COLD;
        int solve_initialize(ErrorHandler *errh) override CLICK_COLD;
        void cleanup(CleanupStage stage) override CLICK_COLD;

        //First : group id, second : destination cpu
        void pre_migrate(DPDKDevice* dev, int from, Vector<Pair<int,int>> gids);
        void post_migrate(DPDKDevice* dev, int from);

        void push_batch(int, PacketBatch* batch) override;

        void init_assignment(Vector<unsigned> table) CLICK_COLD;

        static String read_handler(Element *e, void *thunk);

        void add_handlers() override CLICK_COLD;
    private:
        HashTableMP<IPFlow5ID,int> _hash;
        atomic_uint32_t _current;

        inline void process(Packet* p, BatchBuilder& b);

        FlowControlBlock *fcbs;

        int _table_size;
        int _flow_state_size_full;
        int _verbose;
};

CLICK_ENDDECLS
#endif
