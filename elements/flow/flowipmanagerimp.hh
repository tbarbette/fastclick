#ifndef CLICK_FLOWIPMANAGERIMP_HH
#define CLICK_FLOWIPMANAGERIMP_HH
#include <click/config.h>
#include <click/string.hh>
#include <click/timer.hh>
#include <click/vector.hh>
#include <click/multithread.hh>
#include <click/pair.hh>
#include <click/flow/common.hh>
#include <click/flow/flowelement.hh>
#include <click/batchbuilder.hh>
#include <click/timerwheel.hh>

CLICK_DECLS

class DPDKDevice;
struct rte_hash;

/**
 * FlowIPManagerIMP(CAPACITY [, RESERVE])
 *
 * =s flow
 *  FCB packet classifier - cuckoo per-thread
 *
 * =d
 *
 * Initialize the FCB stack for every packets passing by.
 * The classification is done using a per-core cuckoo hash table.
 *
 * This element does not find automatically the FCB layout for FlowElement,
 * neither set the offsets for placement in the FCB automatically. Look at
 * the middleclick branch for alternatives.
 *
 * =a FlowIPManger
 *
 */
class FlowIPManagerIMP: public VirtualFlowManager, public Router::InitFuture {
    public:
        FlowIPManagerIMP() CLICK_COLD;
        ~FlowIPManagerIMP() CLICK_COLD;

        const char *class_name() const override { return "FlowIPManagerIMP"; }
        const char *port_count() const override { return "1/1"; }

        const char *processing() const override { return PUSH; }
        int configure_phase() const override { return CONFIGURE_PHASE_PRIVILEGED + 1; }

        int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
        int solve_initialize(ErrorHandler *errh) override CLICK_COLD;
        void cleanup(CleanupStage stage) override CLICK_COLD;

        void push_batch(int, PacketBatch* batch) override;
        void run_timer(Timer*) override;
        bool run_task(Task* t) override;

        void add_handlers() override CLICK_COLD;

    protected:
        volatile int owner;
        Packet* queue;


        struct gtable {
            gtable() : hash(0), fcbs(0) {
            }
            rte_hash* hash;
            FlowControlBlock *fcbs;
        } CLICK_ALIGNED(CLICK_CACHE_LINE_SIZE);

        gtable* _tables;

	int _tables_count;
        int _table_size;
        int _flow_state_size_full;
        int _verbose;
        int _flags;

        int _timeout;
        Timer _timer; //Timer to launch the wheel
        Task _task;
        bool _cache;

        static String read_handler(Element* e, void* thunk);
        inline void process(Packet* p, BatchBuilder& b, const Timestamp& recent);
        TimerWheel<FlowControlBlock> _timer_wheel;
};

const auto fim_setter = [](FlowControlBlock* prev, FlowControlBlock* next)
{
    *((FlowControlBlock**)&prev->data_32[2]) = next;
};

CLICK_ENDDECLS
#endif
