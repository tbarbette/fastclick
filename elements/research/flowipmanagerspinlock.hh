#ifndef CLICK_FLOWIPMANAGERSPINLOCK_HH
#define CLICK_FLOWIPMANAGERSPINLOCK_HH
#include <click/config.h>
#include <click/string.hh>
#include <click/timer.hh>
#include <click/vector.hh>
#include <click/multithread.hh>
#include <click/pair.hh>
#include <click/flow/common.hh>
#include <click/flow/flowelement.hh>
#include <click/batchbuilder.hh>
CLICK_DECLS
class DPDKDevice;
struct rte_hash;

template <typename T>
class TimerWheelSpinlock {
    public:
        TimerWheelSpinlock() : _index(0) {
        }

        void initialize(int max) {
            max = next_pow2(max + 2);
            _mask = max - 1;
            _buckets.resize(max);
        }

        inline void schedule_after(T* obj, uint32_t timeout, const std::function<void(T*,T*)> setter) {
            unsigned id = ((*(volatile uint32_t*)&_index) + timeout) & _mask;
            T* f = _buckets.unchecked_at(id);
            setter(obj,f);
            _buckets.unchecked_at(id) = obj;
        }

        inline void schedule_after_mp(T* obj, uint32_t timeout, const std::function<void(T*,T*)> setter) {
            _writers_lock.acquire();
            unsigned id = ((*(volatile uint32_t*)&_index) + timeout) & _mask;

            // click_chatter("Enqueue %p at %d", obj, id);
            T* f = _buckets.unchecked_at(id);
            setter(obj,f);
            _buckets.unchecked_at(id) = obj;
            // click_write_fence(); done by release()
            _writers_lock.release();
        }

        /**
         * Must be called by one thread only!
         */
        inline void run_timers(std::function<T*(T*)> expire) {
            T* f = _buckets.unchecked_at((_index) & _mask);
            //click_chatter("Expire %d -> %d", _index, _index & _mask);
            while (f != 0) {
                f = expire(f);
            }
            _buckets.unchecked_at((_index) & _mask) = 0;
            _index++;
        }

    private:
        uint32_t _mask;
        uint32_t _index;
        Vector<T*> _buckets;
        Spinlock _writers_lock;
};

/**
 * FlowIPManagerSpinlock(CAPACITY [, RESERVE])
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
class FlowIPManagerSpinlock: public VirtualFlowManager, public Router::InitFuture {
    public:
        FlowIPManagerSpinlock() CLICK_COLD;
        ~FlowIPManagerSpinlock() CLICK_COLD;

        const char *class_name() const override { return "FlowIPManagerSpinlock"; }
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
        rte_hash* hash;
        FlowControlBlock *fcbs;

        int _reserve;
        int _table_size;
        int _flow_state_size_full;
        int _verbose;
        int _flags;


        int _timeout;
        Timer _timer; // Timer to launch the wheel
        Task _task;

        static String read_handler(Element* e, void* thunk);
        inline void process(Packet* p, BatchBuilder& b, const Timestamp& recent);
        TimerWheelSpinlock<FlowControlBlock> _timer_wheel;

        // Added the Spinlock to manage multi-thread operations on the flow table
        static Spinlock hash_table_lock;
};

CLICK_ENDDECLS
#endif
