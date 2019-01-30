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
    inline bool get_fcb_for(Packet* &p, FlowControlBlock* &fcb, uint32_t &lastagg, Packet* &last, Packet* &next, Timestamp &now);
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



CLICK_ENDDECLS
#endif
