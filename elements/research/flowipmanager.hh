#ifndef CLICK_FLOWIPMANAGER_HH
#define CLICK_FLOWIPMANAGER_HH
#include <click/config.h>
#include <click/string.hh>
#include <click/timer.hh>
#include <click/vector.hh>
#include <click/multithread.hh>
#include <click/pair.hh>
#include <click/flow/flowelement.hh>
#include <click/flow/common.hh>
#include <click/batchbuilder.hh>
#include <click/timerwheel.hh>
CLICK_DECLS
class DPDKDevice;
struct rte_hash;


/**
 * =c
 * FlowIPManager(CAPACITY [, RESERVE])
 *
 * =s flow
 *  FCB packet classifier - cuckoo shared-by-all-threads
 *
 * =d
 *
 * Initialize the FCB stack for every packets passing by.
 * The classification is done using a unique cuckoo hash table.
 *
 * This element does not find automatically the FCB layout for FlowElement,
 * neither set the offsets for placement in the FCB automatically. Look at
 * the middleclick branch for alternatives.
 *
 * =a FlowIPManger
 *
 */
class FlowIPManager: public VirtualFlowManager, public Router::InitFuture {
    public:
        FlowIPManager() CLICK_COLD;
        ~FlowIPManager() CLICK_COLD;

        const char *class_name() const override { return "FlowIPManager"; }
        const char *port_count() const override { return "1/1"; }

        const char *processing() const override { return PUSH; }
        int configure_phase() const override { return CONFIGURE_PHASE_PRIVILEGED + 1; }
        bool stopClassifier() { return true; };


        int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
        int solve_initialize(ErrorHandler *errh) override CLICK_COLD;
        void cleanup(CleanupStage stage) override CLICK_COLD;

        void push_batch(int, PacketBatch* batch) override;
        void run_timer(Timer*) override;
        bool run_task(Task* t) override;

        void add_handlers() override CLICK_COLD;

    protected:
        #define FCB_DATA(fcb, offset) (((uint8_t *)(fcb->data_32)) + (offset))

        volatile int owner;
        Packet* queue;
        rte_hash* hash;
        FlowControlBlock *fcbs;

        int _table_size;
        int _flow_state_size_full;
        int _verbose;
        int _flags;

        uint32_t _recycle_interval_ms;
        uint32_t _timeout_ms;
        FlowControlBlock* _qbsr;
        uint32_t _timeout_epochs;
        uint32_t _epochs_per_sec;      // Granularity for the epoch
        Timer _timer; //Timer to launch the wheel
        Task _task;

        bool _cache;

        static String read_handler(Element* e, void* thunk);
        inline void process(Packet* p, BatchBuilder& b, const Timestamp& recent);
        TimerWheel<FlowControlBlock> _timer_wheel;
};

CLICK_ENDDECLS
#endif
