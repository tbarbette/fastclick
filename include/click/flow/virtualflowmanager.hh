#ifndef CLICK_VFLOWMANAGERIMP_HH
#define CLICK_VFLOWMANAGERIMP_HH
#include <click/flow/flowelement.hh>
#include <click/timerwheel.hh>
#include <click/batchbuilder.hh>
#include <type_traits>

class _FlowManagerIMPState { public:
   //The table of FCBs
    FlowControlBlock *fcbs;
    TimerWheel<FlowControlBlock> _timer_wheel;
    Timer* maintain_timer;
};

class FlowManagerIMPStateNoFID : public _FlowManagerIMPState { public:
 
    static constexpr bool need_fid() {
        return false;
    }

};


//gtable must extends the IMPState
class FlowManagerIMPState : public _FlowManagerIMPState { public:

    // Flow stack management
    uint32_t *flows_stack = 0;
    int flows_stack_i;
    FlowControlBlock* _qbsr;

    static constexpr bool need_fid() {
        return true;
    }

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
        int timeout = 0;
        int ret =
            (*args)
                .read_or_set_p("CAPACITY", _capacity, 65536) // HT capacity
                .read_or_set("RESERVE", _reserve, 0)
                .read_or_set("VERBOSE", _verbose, 0)
                .read_or_set("CACHE", _cache, 1)
                .read_or_set("TIMEOUT", timeout, 0) // Timeout for the entries
                .read_or_set("RECYCLE_INTERVAL", recycle_interval, 1)
                .consume();

        _recycle_interval_ms = (int)(recycle_interval * 1000);
        _epochs_per_sec = max(1, 1000 / _recycle_interval_ms);
        _timeout_ms = timeout * 1000;
        _timeout_epochs = timeout * _epochs_per_sec;

        //_reserve += reserve_size() is called by the parrent

        return ret;
    }

    int solve_initialize(ErrorHandler *errh) override
    {        
        auto passing = get_passing_threads();
        int table_count = passing.size();
        _capacity = next_pow2(_capacity/passing.weight());
        _tables.compress(passing);

        click_chatter("Real capacity for each table will be %d", _capacity);
        assert(_reserve >= reserve_size());
        _flow_state_size_full = sizeof(FlowControlBlock) + _reserve;

        for (int ui = 0; ui < _tables.weight(); ui++) {
            
            State &t = _tables.get_value(ui);
            int core = _tables.get_mapping(ui);

            if (((T*)this)->alloc(t,core,errh) != 0) {
                return -1;
            }

            if (_timeout_epochs > 0) {
                t._timer_wheel.initialize(_timeout_epochs);
            }

            t.fcbs =  (FlowControlBlock*)CLICK_ALIGNED_ALLOC(_flow_state_size_full * _capacity);
            CLICK_ASSERT_ALIGNED(t.fcbs);
            bzero(t.fcbs,_flow_state_size_full * _capacity);
            if (!t.fcbs) {
                return errh->error("Could not init data for table core %d!", core);
            }
            
            if constexpr (State::need_fid()) {
                t.flows_stack = (uint32_t *)CLICK_ALIGNED_ALLOC(sizeof(uint32_t) * (_capacity + 1));
                for (int i =0; i < _capacity; i++)
                    t.imp_flows_push(i);
            }

             if (_timeout_ms > 0 && have_maintainer) {
                //click_chatter("Initializing maintain timer %d", core);
                t.maintain_timer = new Timer(this);
                t.maintain_timer->initialize(this, true);
                t.maintain_timer->assign(run_maintain_timer, this);
                t.maintain_timer->move_thread(core);
                t.maintain_timer->schedule_after_msec(_recycle_interval_ms);
            } else {
                click_chatter("%p{element} does not have timeout enabled. Another mean must be used to remove old flows.", this);
            }

        }

        return Router::InitFuture::solve_initialize(errh);
    }

    static void run_maintain_timer(Timer *t, void *thunk) {
        
        int core = click_current_cpu_id();
        VirtualFlowManagerIMP *e =
            reinterpret_cast<VirtualFlowManagerIMP *>(thunk);

        int removed = e->maintainer();
        //click_chatter("Run timer, removed %d", removed);

        e->_tables.get_value_for_thread(core).maintain_timer->schedule_after_msec(e->_recycle_interval_ms);
    }
    
    inline void update_lastseen(FlowControlBlock *fcb, Timestamp &ts) {
         fcb->lastseen = ts;
    }

    
    inline int maintainer() {
        Timestamp recent = Timestamp::recent_steady();
        int dest_core = click_current_cpu_id();
        State &state = *_tables;
        if constexpr (State::need_fid()) {
            while (state._qbsr) {
                FlowControlBlock* next = *get_next_released_fcb(state._qbsr);
                state.imp_flows_push(*get_fcb_flowid(state._qbsr));
                state._qbsr = next;
            }
        }

        int checker = 0;
        TimerWheel<FlowControlBlock> &tw = state._timer_wheel;
        tw.run_timers([this,recent,&checker, &tw, &state, dest_core](FlowControlBlock* prev) -> FlowControlBlock* {
            if (unlikely(checker >= _capacity))
            {
                click_chatter("Loop detected!");
                abort();
            }
            FlowControlBlock * next = *get_next_released_fcb(prev);
            //Verify lastseen is not in the future
           // click_chatter("%d %d, next %p", recent, prev->lastseen, next);
            if (unlikely(recent <= prev->lastseen)) {

                int64_t old = (recent - prev->lastseen).msecval();
                click_chatter("Old %li : %s %s fid %d",old, recent.unparse().c_str(), prev->lastseen.unparse().c_str(),get_fcb_flowid(prev) );

                tw.schedule_after(prev, _timeout_epochs,  setter);
                return next;
            }

            int old = (recent - prev->lastseen).msecval();

            if (old + _recycle_interval_ms >= _timeout_ms) {

                //expire
                //click_chatter("Release %p", prev);

                int pos = ((T*)this)->remove(*get_fcb_key(prev));
                if constexpr (State::need_fid()) {
                    if (likely(pos==0))
                    {
                        *get_next_released_fcb(prev) = state._qbsr;
                        state._qbsr = prev;
                    }
                    else
                    {
                        click_chatter("[%d->%d] error %d Removed a key not in the table flow %d (%s)...", click_current_cpu_id(), dest_core , pos , *get_fcb_flowid(prev), get_fcb_key(prev)->unparse().c_str());
                    }
                } else {
                     
                }

                checker++;
            } else {
                //No need for lock as we'll be the only one to enqueue there
                if (likely(prev != *get_next_released_fcb(prev))) {
                    int r = (_timeout_ms) - old; //Time left in ms
                    r = (r * (_epochs_per_sec)) / 1000;
                    tw.schedule_after(prev, r, setter);
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

    void push_batch(int, PacketBatch *batch) override {
        BatchBuilder b;
        Timestamp recent;
        FlowControlBlock* tmp = fcb_stack;
        if (have_maintainer)
            recent = Timestamp::recent_steady();

        FOR_EACH_PACKET_SAFE(batch, p) {
                ((T*)this)->process(p, b, recent);
        }

        batch = b.finish();
        if (batch) {
            if (have_maintainer && _timeout_epochs)
                update_lastseen(fcb_stack, recent);
            #if HAVE_FLOW_DYNAMIC
                fcb_acquire(batch->count());
            #endif
            output_push_batch(0, batch);
        }
        fcb_stack = tmp;
    }


inline void process(Packet *p, BatchBuilder &b, Timestamp &recent) {
    IPFlow5ID fid = IPFlow5ID(p);
    if (_cache && fid == b.last_id) {
        b.append(p);
        return;
    }

    FlowControlBlock *fcb;
    auto &state = *_tables;

    int ret = ((T*)this)->find(fid);

    if (ret <= 0) { 
        uint32_t flowid;
        if constexpr (State::need_fid()) {
            flowid = state.imp_flows_pop();
            if (unlikely(flowid == 0)) {
                click_chatter("ID is 0 and table is full!");
                p->kill();
                return;
            }

            // INSERT IN TABLE
        
            ret = ((T*)this)->insert(fid, flowid);

        } else {
            
            ret = ((T*)this)->insert(fid, 0);

            flowid = ret;
        }

        if (unlikely(ret < 0)) {
            p->kill();
            return;
        }
        fcb = get_fcb_from_flowid(ret);


        if constexpr (State::need_fid()) {
            *(get_fcb_flowid(fcb)) = flowid;
        }

        if (_timeout_epochs) {
            memcpy(get_fcb_key(fcb), &fid, sizeof(IPFlow5ID));
            state._timer_wheel.schedule_after(fcb, _timeout_epochs, setter);
        }

    } // (end)It's a new flow
    else
    { // Old flow
        fcb = get_fcb_from_flowid(ret);
    }

    if (b.last == ret) {
        b.append(p);
    } else {
        PacketBatch *batch;
        batch = b.finish();


        if (batch) {
            if (have_maintainer && _timeout_epochs)
                update_lastseen(fcb_stack, recent);
            #if HAVE_FLOW_DYNAMIC
                fcb_acquire(batch->count());
            #endif
            output_push_batch(0, batch);
        }
        fcb_stack = fcb;
        b.init();
        b.append(p);
        b.last = ret;
        if (_cache) {
            b.last_id = fid;
        }
    }
}


protected:

    #define FCB_DATA(fcb, offset) (((uint8_t *)(fcb->data_32)) + (offset))

    static inline uint32_t *get_fcb_flowid(FlowControlBlock *fcb) {
        if constexpr (!State::need_fid()) {
            assert(false);
        }
        return (uint32_t *)FCB_DATA(fcb, 0);
    };

    /**
     * Returns the location of the IPFlow5ID in the FCB
     */
    static inline IPFlow5ID *get_fcb_key(FlowControlBlock *fcb) {
        return (IPFlow5ID *)FCB_DATA(fcb, (State::need_fid()? sizeof(uint32_t) : 0 ));
    };

    inline FlowControlBlock* get_fcb_from_flowid(int i) {
        return (FlowControlBlock *)(((uint8_t *)_tables->fcbs) +
                                _flow_state_size_full * i);
    }


    inline static const int reserve_size() {
        if constexpr (State::need_fid()) {
             return sizeof(uint32_t) + sizeof(IPFlow5ID) + sizeof(FlowControlBlock*);
        } else {
             return sizeof(IPFlow5ID) + sizeof(FlowControlBlock*);
        }
       
    }

    inline static FlowControlBlock** get_next_released_fcb(FlowControlBlock *fcb) {
	    return (FlowControlBlock**) FCB_DATA(fcb, reserve_size() - sizeof(FlowControlBlock*));
    };

    
    const static auto constexpr setter = [](FlowControlBlock* prev, FlowControlBlock* next)
    {
        *(get_next_released_fcb(prev)) = next;
    };
    


    enum {
        h_count,
        h_count_fids,
        h_capacity,
        h_total_capacity
    };

    static String read_handler(Element *e, void *thunk) {
        T *f = static_cast<T *>(e);

        intptr_t cnt = (intptr_t)thunk;
        switch (cnt) {
        case h_count:
            return String(f->count());
        case h_count_fids:
                if constexpr (State::need_fid()) {
            return String(f->_tables->flows_stack_i);

         }  else return 0;
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
        add_read_handler("count_fids", read_handler, h_count_fids);
        add_read_handler("capacity", read_handler, h_capacity);
        add_read_handler("total_capacity", read_handler, h_total_capacity);
    }

    per_thread_oread<State> _tables;
    uint32_t _capacity;
    int _verbose;
    uint32_t _flow_state_size_full;
    bool _cache;
    uint32_t _timeout_epochs;
    uint32_t _timeout_ms;          // Timeout for deletion
    uint32_t _epochs_per_sec;      // Granularity for the epoch
    uint16_t _recycle_interval_ms; // When to run the maintainer
    const bool have_maintainer = true;
};

#endif
