#ifndef CLICK_FLOWCLASSIFIER_HH
#define CLICK_FLOWCLASSIFIER_HH
#include <click/flowelement.hh>
#include <click/string.hh>
#include <click/timer.hh>
#include <click/flow.hh>
#include <click/multithread.hh>
#include <vector>


CLICK_DECLS

typedef struct {
    PacketBatch* batch;
    FlowControlBlock* fcb;
} FlowBatch;

typedef struct FlowCache_t{
    uint32_t agg;
    FlowControlBlock* fcb;
} FlowCache;

class FlowClassifier: public FlowElement {
protected:
    FlowClassificationTable _table;
    per_thread<FlowCache*> _cache;
    int _cache_size;
    int _cache_ring_size;
    int _cache_mask;
    bool _aggcache;
    int _pull_burst;
    int _verbose;
    int _size_verbose;
    int _builder;
    int _builder_mask;
    bool _collision_is_life;
    int cache_hit;
    int cache_sharing;
    int cache_miss;
    int _clean_timer;
    Timer _timer;
    bool _early_drop;
    FlowType _context;
#if HAVE_FLOW_DYNAMIC
    static constexpr bool _do_release = true;
#else
    static constexpr bool _do_release = false;
#endif
    bool _ordered;
    bool _nocut;

    per_thread<FlowBatch*> _builder_batch;
    static int _n_classifiers;
    int _reserve;
    static Vector<FlowClassifier *> _classifiers;
    typedef Pair<Element*,int> EDPair;
    Vector<EDPair>  _reachable_list;

    int _pool_data_size;


    void build_fcb();
public:
    FlowClassifier() CLICK_COLD;

	~FlowClassifier() CLICK_COLD;

    const char *class_name() const		{ return "FlowClassifier"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return DOUBLE; }
    int configure_phase() const     { return CONFIGURE_PHASE_PRIVILEGED + 1; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh) CLICK_COLD;
    void cleanup(CleanupStage stage) CLICK_COLD;

    bool stopClassifier() override CLICK_COLD { return true; };

    inline int cache_find(FlowControlBlock* fcb);
    inline FlowControlBlock* set_fcb_cache(FlowCache* &c, Packet* &p, const uint32_t& agg);
    inline void remove_cache_fcb(FlowControlBlock* fcb);
    inline FlowControlBlock* get_cache_fcb(Packet* p, uint32_t agg);
    inline bool get_fcb_for(Packet* &p, FlowControlBlock* &fcb, uint32_t &lastagg, Packet* &last, Packet* &next, const Timestamp &now);
    inline void push_batch_simple(int port, PacketBatch*);
    inline void push_batch_builder(int port, PacketBatch*);
    void push_batch(int port, PacketBatch*);
    static String read_handler(Element* e, void* thunk);
    void add_handlers() override CLICK_COLD;

    inline bool is_dynamic_cache_enabled() {
        return _aggcache && _cache_size > 0;
    }

    virtual FlowNode* get_table(int iport, Vector<FlowElement*> contextStack) {
        click_chatter("Warning : Sub-table optimization not supported as of now.");
        return 0;
    }

#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
    void run_timer(Timer*) override;
#endif
    bool run_idle_task(IdleTask*) override;

    virtual FlowNode* resolveContext(FlowType t, Vector<FlowElement*> contextStack) override;

	FlowClassificationTable& table() {
		return _table;
	}

protected:
    int _initialize_timers(ErrorHandler *errh);
    int _replace_leafs(ErrorHandler *errh);
    int _initialize_classifier(ErrorHandler *errh);

inline void flush_simple(Packet* &last, PacketBatch* awaiting_batch, int &count, const Timestamp &now);
inline void handle_simple(Packet* &p, Packet* &last, FlowControlBlock* &fcb, PacketBatch* &awaiting_batch, int &count, const Timestamp &now);

    inline bool is_valid_fcb(Packet* &p, Packet* &last, Packet* &next, FlowControlBlock* &fcb, const Timestamp &now);
};

inline void FlowClassifier::handle_simple(Packet* &p, Packet* &last, FlowControlBlock* &fcb, PacketBatch* &awaiting_batch, int &count, const Timestamp &now) {
        if (awaiting_batch == NULL) {
#if DEBUG_CLASSIFIER > 1
            click_chatter("New fcb %p",fcb);
#endif
            fcb_stack = fcb;
            awaiting_batch = PacketBatch::start_head(p);
        } else {
            if ((_nocut && fcb_stack) || fcb == fcb_stack) {
#if DEBUG_CLASSIFIER > 1
        click_chatter("Same fcb %p",fcb);
#endif
                //Do nothing as we follow the LL
            } else {
#if DEBUG_CLASSIFIER > 1
        click_chatter("Different fcb %p, last was %p",fcb,fcb_stack);
#endif
#if HAVE_FLOW_DYNAMIC
                fcb_stack->acquire(count);
#endif
                last->set_next(0);
                awaiting_batch->set_tail(last);
                awaiting_batch->set_count(count);
                fcb_stack->lastseen = now;
                output_push_batch(0, awaiting_batch);
                awaiting_batch = PacketBatch::start_head(p);
                fcb_stack = fcb;
                count = 0;
            }
        }

        count ++;
}

inline void FlowClassifier::flush_simple(Packet* &last, PacketBatch* awaiting_batch, int &count, const Timestamp &now) {
    if (awaiting_batch) {
#if HAVE_FLOW_DYNAMIC
        fcb_stack->acquire(count);
#endif
        fcb_stack->lastseen = now;
        last->set_next(0);
        awaiting_batch->set_tail(last);
        awaiting_batch->set_count(count);
        output_push_batch(0,awaiting_batch);
        fcb_stack = 0;
    }

}

/**
 * Checks that the FCB has not timed out
 */
static inline void check_fcb_still_valid(FlowControlBlock* fcb, Timestamp now) {
#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
            if (unlikely(fcb->count() == 0 && fcb->hasTimeout())) {
# if DEBUG_CLASSIFIER_TIMEOUT > 0
                assert(fcb->flags & FLOW_TIMEOUT_INLIST);
# endif
                if (fcb->timeoutPassed(now)) {
# if DEBUG_CLASSIFIER_TIMEOUT > 1
                    click_chatter("Timeout of %p passed or released and is now seen again, reinitializing timer",fcb);
# endif
                    //Do not call initialize as everything is still set, just reinit timeout
                    fcb->flags = FLOW_TIMEOUT | FLOW_TIMEOUT_INLIST;
                } else {
# if DEBUG_CLASSIFIER_TIMEOUT > 1
                    click_chatter("Timeout recovered, keeping the flow %p",fcb);
# endif
                }
            } else {
# if DEBUG_CLASSIFIER_TIMEOUT > 1
                click_chatter("Fcb %p Still valid : Fcb count is %d and hasTimeout %d",fcb, fcb->count(),fcb->hasTimeout());
# endif
            }
#endif
}


/**
 * Check that the FCB is not null, drop the packet if it is an early drop,
 *  then check its timeout with check_fcb_still_valid.
 */
inline bool FlowClassifier::is_valid_fcb(Packet* &p, Packet* &last, Packet* &next, FlowControlBlock* &fcb, const Timestamp &now) {
    if (unlikely(_verbose > 2)) {
        if (_verbose > 3) {
            click_chatter("Table of %s after getting fcb %p :",name().c_str(),fcb);
        } else {
            click_chatter("Table of %s after getting new packet (length %d) :",name().c_str(),p->length());
        }
        _table.get_root()->print(-1,_verbose > 3);
    }
    if (unlikely(!fcb || (fcb->is_early_drop() && _early_drop))) {
        if (_verbose > 1)
            debug_flow("Early drop !");
        if (last) {
            last->set_next(next);
        }
        SFCB_STACK(p->kill(););
        p = next;
        return false;
    }

    check_fcb_still_valid(fcb, now);
    return true;
}


CLICK_ENDDECLS
#endif
