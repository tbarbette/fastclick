#ifndef CLICK_VFLOWMANAGERIMP_HH
#define CLICK_VFLOWMANAGERIMP_HH
#include <click/flow/flowelement.hh>
#include <click/timerwheel.hh>


//gtable must extends the IMPState
class FlowManagerIMPState { public:
    //The table of FCBs
    FlowControlBlock *fcbs;
    // Flow stack management
    uint32_t *flows_stack = 0;
    int flows_stack_i;
    TimerWheel<FlowControlBlock*> _timer_wheel;
    FlowControlBlock* _qbsr;

    inline uint32_t imp_flows_pop() {
        return flows_stack[flows_stack_i--];
    }

    inline void imp_flows_push(uint32_t flow) {    
        flows_stack[++flows_stack_i] = flow;
    }

    inline bool imp_flows_empty() {
        return flows_stack_i < 0;
    }
};

/**
 * Element that allocates some FCB Space per-thread
 */
template<typename T, class State> 
class VirtualFlowManagerIMP : public VirtualFlowManager, public Router::InitFuture { public:
    VirtualFlowManagerIMP() : _tables(), _cache(true) {
        
    };
    
    int parse(Args *args) {
        double recycle_interval = 0;
        int ret =
            (*args)
                .read_or_set_p("CAPACITY", _capacity, 65536) // HT capacity
                .read_or_set("RESERVE", _reserve, 0)
                .read_or_set("VERBOSE", _verbose, 0)
                .read_or_set("CACHE", _cache, 1)
                .read_or_set("TIMEOUT", _timeout, 0) // Timeout for the entries
                .read_or_set("RECYCLE_INTERVAL", recycle_interval, 1)
//                .read_or_set("FLOW_BATCH", _flows_batchsize, 16)
                .consume();

        _recycle_interval_ms = (int)(recycle_interval * 1000);
        _epochs_per_sec = max(1, 1000 / _recycle_interval_ms);
        _timeout_ms = _timeout * 1000;

        return ret;
    }

    int solve_initialize(ErrorHandler *errh) override
    {        
        auto passing = get_passing_threads();
        int table_count = passing.size();
        _capacity = next_pow2(_capacity/passing.weight());
        _tables.compress(passing);

        click_chatter("Real capacity for each table will be %d", _capacity);
        _flow_state_size_full = sizeof(FlowControlBlock) + _reserve;

        for (int ui = 0; ui < _tables.weight(); ui++) {
            


            auto &t = _tables.get_value(ui);
            int core = _tables.get_mapping(ui);

            if (((T*)this)->alloc(t,core,errh) != 0) {
                return -1;
            }

            if (_timeout > 0) {
                t._timer_wheel.initialize(_timeout * _epochs_per_sec);
            }

            t.fcbs =  (FlowControlBlock*)CLICK_ALIGNED_ALLOC(_flow_state_size_full * _capacity);
            CLICK_ASSERT_ALIGNED(t.fcbs);
            bzero(t.fcbs,_flow_state_size_full * _capacity);
            if (!t.fcbs) {
                return errh->error("Could not init data for table core %d!", core);
            }
            t.flows_stack = (uint32_t *)CLICK_ALIGNED_ALLOC(sizeof(uint32_t) * (_capacity + 1));
            for (int i =0; i < _capacity; i++)
                t.imp_flows_push(i);

        }

        return Router::InitFuture::solve_initialize(errh);
    }

    inline void update_lastseen(FlowControlBlock *fcb, Timestamp &ts) {
         fcb->lastseen = ts;
    }

    
    static const auto released_fcb_next_setter = [](FlowControlBlock* prev, FlowControlBlock* next)
    {
        *(T::get_next_released_fcb(prev)) = next;
    };

    int maintainer() {
        Timestamp recent = Timestamp::recent_steady();
        int dest_core = click_current_cpu_id();
        auto &state = _tables;
        while (state._qbsr) {
            FlowControlBlock* next = *get_next_released_fcb(state._qbsr);
            flows_push(dest_core, *get_fcb_flowid(state._qbsr));
            state._qbsr = next;
        }

        int checker = 0;
        auto &tw = state._timer_weel;
        tw.run_timers([this,recent,&checker, tw, state, dest_core](FlowControlBlock* prev) -> FlowControlBlock* {
            if (unlikely(checker >= _capacity))
            {
                click_chatter("Loop detected!");
                abort();
            }
            FlowControlBlock * next = *get_next_released_fcb(prev);
            //Verify lastseen is not in the future
            if (unlikely(recent <= prev->lastseen)) {

                int64_t old = (recent - prev->lastseen).msecval();
                click_chatter("Old %li : %s %s fid %d",old, recent.unparse().c_str(), prev->lastseen.unparse().c_str(),get_fcb_flowid(prev) );

                tw.schedule_after(prev, _timeout * _epochs_per_sec, released_fcb_next_setter);
                return next;
            }

            int old = (recent - prev->lastseen).msecval();

            if (old + _recycle_interval_ms >= _timeout * 1000) {

                //expire

                int pos = T::remove(get_fcb_key(prev));
                if (likely(pos==0))
                {
                    *get_next_released_fcb(prev) = state._qbsr;
                    state._qbsr = prev;
                }
                else
                {
                    click_chatter("[%d->%d] error %d Removed a key not in the table flow %d (%s)...", click_current_cpu_id(), dest_core , pos , *get_fcb_flowid(prev), get_fcb_key(prev)->unparse().c_str());
                }

                checker++;
            } else {
                //No need for lock as we'll be the only one to enqueue there
                if (likely(prev != *get_next_released_fcb(prev))) {
                    int r = (_timeout * 1000) - old; //Time left in ms
                    r = (r * (_epochs_per_sec)) / 1000;
                    tw.schedule_after(prev, r, released_fcb_next_setter);
                }
                else
                {
                    click_chatter("Looping on the same entry. do not schedule!");
                    abort();
                }
            }
            return next;
        });

        return checker;
    }


protected:

    #define FCB_DATA(fcb, offset) (((uint8_t *)(fcb->data_32)) + offset)

    static inline uint32_t *get_fcb_flowid(FlowControlBlock *fcb) {
        return (uint32_t *)FCB_DATA(fcb, 0);
    };

    static inline IPFlow5ID *get_fcb_key(FlowControlBlock *fcb) {
        return (IPFlow5ID *)FCB_DATA(fcb, sizeof(uint32_t));
    };

    inline FlowControlBlock* get_fcb_from_flowid(int i) {
        return (FlowControlBlock *)(((uint8_t *)_tables->fcbs) +
                                _flow_state_size_full * i);
    }


    inline static const int reserve_size() {
        return sizeof(uint32_t) + sizeof(IPFlow5ID);
    }

    inline static FlowControlBlock** get_next_released_fcb(FlowControlBlock *fcb) {
	    return (FlowControlBlock**) FCB_DATA(fcb, reserve_size() );
    };


    enum {
        h_count,
        h_capacity,
        h_total_capacity
    };

    static String read_handler(Element *e, void *thunk) {
        T *f = static_cast<T *>(e);

        intptr_t cnt = (intptr_t)thunk;
        switch (cnt) {
        case h_count:
            return String(f->count());
        case h_capacity:
            return String(f->_capacity);
        case h_total_capacity:
            return String(f->_capacity);
        default:
            return "<error>";
        }
    }

    void add_handlers() {
        add_read_handler("count", read_handler, h_count);
        add_read_handler("capacity", read_handler, h_capacity);
        add_read_handler("total_capacity", read_handler, h_total_capacity);
    }

    per_thread_oread<State> _tables;
    uint32_t _capacity;
    int _verbose;
    uint32_t _timeout;
    uint32_t _flow_state_size_full;
    bool _cache;
    uint32_t _timeout_ms;          // Timeout for deletion
    uint32_t _epochs_per_sec;      // Granularity for the epoch
    uint16_t _recycle_interval_ms; // When to run the maintainer
    const bool have_maintainer = true;
};

#endif