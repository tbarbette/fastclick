#ifndef CLICK_FLOWCLASSIFIER_HH
#define CLICK_FLOWCLASSIFIER_HH
#include <click/string.hh>
#include <click/timer.hh>
#include <click/multithread.hh>
#include <vector>
#include <click/flow/flow.hh>
#include <click/flow/flowelement.hh>
#include <click/flow/ctxelement.hh>


CLICK_DECLS


#define USE_CACHE_RING 1

typedef struct {
    PacketBatch* batch;
    FlowControlBlock* fcb;
} FlowBatch;

typedef struct FlowCache_t{
    uint32_t agg;
    FlowControlBlock* fcb;
} FlowCache;

class CTXManager: public VirtualFlowManager, public Router::InitFuture  {
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
    bool _optimize;
    FlowType _context;
#if HAVE_FLOW_DYNAMIC
    bool _do_release;
#else
    static constexpr bool _do_release = false;
#endif
    bool _ordered;
    bool _nocut;

    per_thread<FlowBatch*> _builder_batch;


    void build_fcb();
public:
    CTXManager() CLICK_COLD;

	~CTXManager() CLICK_COLD;

    const char *class_name() const		{ return "CTXManager"; }
    const char *port_count() const		{ return "1/1"; }
//    const char *processing() const		{ return DOUBLE; }

    const char *processing() const		{ return PUSH; }
    int configure_phase() const     { return CONFIGURE_PHASE_PRIVILEGED + 1; }

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
    virtual int solve_initialize(ErrorHandler *errh) override CLICK_COLD;
    void cleanup(CleanupStage stage) override CLICK_COLD;

    bool stopClassifier() override CLICK_COLD { return true; };

    inline int cache_find(FlowControlBlock* fcb);
    inline FlowControlBlock* set_fcb_cache(FlowCache* &c, Packet* &p, const uint32_t& agg);
    inline void remove_cache_fcb(FlowControlBlock* fcb);
    inline FlowControlBlock* get_cache_fcb(Packet* p, uint32_t agg, FlowNode* root);
    inline bool get_fcb_for(Packet* &p, FlowControlBlock* &fcb, uint32_t &lastagg, Packet* &last, Packet* &next, const Timestamp &now);
    inline void push_batch_simple(int port, PacketBatch*);
    inline void push_batch_builder(int port, PacketBatch*);
    void push_batch(int port, PacketBatch*);
    static String read_handler(Element* e, void* thunk);
    void add_handlers() override CLICK_COLD;

    virtual void fcb_built();

    static CounterInitFuture* ctx_builded_init_future() {
        return &_ctx_builded_init_future;
    }

    inline bool is_dynamic_cache_enabled() {
        return _aggcache && _cache_size > 0;
    }

    virtual FlowNode* get_table(int iport, Vector<FlowElement*> contextStack) {
        click_chatter("Warning : Sub-table optimization not supported as of now.");
        return 0;
    }

#if HAVE_CTX_GLOBAL_TIMEOUT
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

#define BUILDER_RING_SIZE 16

struct Builder {
    FlowBatch batches[BUILDER_RING_SIZE];
    int head = 0;
    int tail = 0;
    int curbatch = -1;
    int count = 0;
    FlowControlBlock* lastfcb = 0;
    Packet* last = NULL;
};

inline void flush_builder(Packet* &last, Builder &builder, const Timestamp &now);
inline void handle_builder(Packet* &p, Packet* &last, FlowControlBlock* &fcb, Builder &builder, const Timestamp &now);

inline bool is_valid_fcb(Packet* &p, Packet* &last, Packet* &next, FlowControlBlock* &fcb, const Timestamp &now);
inline void check_release_flows();
};

inline FlowControlBlock* CTXManager::set_fcb_cache(FlowCache* &c, Packet* &p, const uint32_t& agg) {
    FlowControlBlock* fcb = _table.match(p);

    if (*((uint32_t*)&(fcb->node_data[1])) == 0) {
        *((uint32_t*)&(fcb->node_data[1])) = agg;
        c->fcb = fcb;
        c->agg = agg;
    } else {
//        click_chatter("FCB already in cache with different AGG %u<->%u FCB %p dynamic %d",agg,*((uint32_t*)&(fcb->node_data[1])),fcb, fcb->dynamic);
    }
    return fcb;
}

inline bool CTXManager::get_fcb_for(Packet* &p, FlowControlBlock* &fcb, uint32_t &lastagg, Packet* &last, Packet* &next, const Timestamp &now) {
    if (_aggcache) {
        uint32_t agg = AGGREGATE_ANNO(p);
        if (!(lastagg == agg && fcb && likely(fcb->parent && _table.reverse_match(fcb,p, _table.get_root())))) {
            if (_cache_size > 0)
                fcb = get_cache_fcb(p,agg,_table.get_root());
            else
                fcb = _table.match(p);
            lastagg = agg;
        }
    }
    else
    {
        fcb = _table.match(p);
    }
    return is_valid_fcb(p, last, next, fcb, now);
}

inline void CTXManager::check_release_flows() {
    if (_do_release) {
#if HAVE_CTX_GLOBAL_TIMEOUT
        auto &head = _table.old_flows.get();
        if (head.count() > head._count_thresh) {
#if DEBUG_CLASSIFIER_TIMEOUT > 0
            click_chatter("%p{element} Forced release because %d is > than %d",this,head.count(), head._count_thresh);
#endif
            _table.check_release();
            if (unlikely(head.count() < (head._count_thresh / 8) && head._count_thresh > FlowTableHolder::fcb_list::DEFAULT_THRESH)) {
                head._count_thresh /= 2;
            } else
                head._count_thresh *= 2;
#if DEBUG_CLASSIFIER_TIMEOUT > 0
            click_chatter("%p{element} Forced release count %d thresh %d",this,head.count(), head._count_thresh);
#endif
        }
#endif
    }

}

inline void CTXManager::handle_simple(Packet* &p, Packet* &last, FlowControlBlock* &fcb, PacketBatch* &awaiting_batch, int &count, const Timestamp &now) {
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

inline void CTXManager::flush_simple(Packet* &last, PacketBatch* awaiting_batch, int &count, const Timestamp &now) {
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
 * Rebuild batches of packets of the same flow using a ring. The ring is composed of linked-list of batches of packets of the same flow.
 *   for each packet we try to insert it in the ring, or take a new entry. When we wrap-up we force flushing the first batch.
 * @arg p The packet
 * @arg last The last classified packet, used to avoid rebuilding a LL if the link is still valid
 * @arg fcb FCB of the packet
 * @arg builder Builder element to keep track of the ring.
 * @arg timestamp now Current timestamp. Just to avoid re-computing it more often than needed.
 */
inline void CTXManager::handle_builder(Packet* &p, Packet* &last, FlowControlBlock* &fcb, Builder &builder, const Timestamp &now) {
        if ((_nocut && builder.lastfcb) || builder.lastfcb == fcb) {
            //Just continue as they are still linked
        } else {

                //Break the last flow
                if (last) {
                    last->set_next(0);
                    builder.batches[builder.curbatch].batch->set_count(builder.count);
                    builder.batches[builder.curbatch].batch->set_tail(last);
                }

                //Find a potential match
                for (int i = builder.tail; i < builder.head; i++) {
                    if (builder.batches[i % BUILDER_RING_SIZE].fcb == fcb) { //Flow already in list, append
                        //click_chatter("Flow already in list");
                        builder.curbatch = i % BUILDER_RING_SIZE;
                        builder.count = builder.batches[builder.curbatch].batch->count();
                        last = builder.batches[builder.curbatch].batch->tail();
                        last->set_next(p);
                        goto attach;
                    }
                }
                //click_chatter("Unknown fcb %p, curbatch = %d",fcb,head);
                builder.curbatch = builder.head % BUILDER_RING_SIZE;
                builder.head++;

                if (builder.tail % BUILDER_RING_SIZE == builder.head % BUILDER_RING_SIZE) {
                    auto &b = builder.batches[builder.tail % BUILDER_RING_SIZE];
                    if (_verbose > 1) {
                        click_chatter("WARNING (unoptimized) Ring full with batch of %d packets, processing now !", b.batch->count());
                    }
                    //Ring full, process batch NOW
                    fcb_stack = b.fcb;
#if HAVE_FLOW_DYNAMIC
                    fcb_stack->acquire(b.batch->count());
#endif
                    fcb_stack->lastseen = now;
                    //click_chatter("FPush %d of %d packets",tail % BUILDER_RING_SIZE,batches[tail % BUILDER_RING_SIZE].batch->count());
                    output_push_batch(0,b.batch);
                    builder.tail++;
                }
                //click_chatter("batches[%d].batch = %p",curbatch,batch);
                //click_chatter("batches[%d].fcb = %p",curbatch,fcb);
                builder.batches[builder.curbatch].batch = PacketBatch::start_head(p);
                builder.batches[builder.curbatch].fcb = fcb;
                builder.count = 0;
        }
        attach:
        builder.count ++;
        builder.lastfcb = fcb;
}


inline void CTXManager::flush_builder(Packet* &last, Builder &builder, const Timestamp &now) {
    if (last) {
        last->set_next(0);
        builder.batches[builder.curbatch].batch->set_tail(last);
        builder.batches[builder.curbatch].batch->set_count(builder.count);
    }

    //click_chatter("%d batches :",head-tail);
   // for (int i = tail;i < head;i++) {
        //click_chatter("[%d] %d packets for fcb %p",i,batches[i%BUILDER_RING_SIZE].batch->count(),batches[i%BUILDER_RING_SIZE].fcb);
    //}
    for (;builder.tail < builder.head;) {
        auto &b = builder.batches[builder.tail % BUILDER_RING_SIZE];
        fcb_stack = b.fcb;

#if HAVE_FLOW_DYNAMIC
        fcb_stack->acquire(b.batch->count());
#endif
        fcb_stack->lastseen = now;
        //click_chatter("EPush %d of %d packets",tail % BUILDER_RING_SIZE,batches[tail % BUILDER_RING_SIZE].batch->count());
        output_push_batch(0,b.batch);

        builder.tail++;
        if (builder.tail == builder.head) break;

        //Double mode
        /*
        if (input_is_pull(0) && ((batch = input_pull_batch(0,_pull_burst)) != 0)) { //If we can pull and it's not the last pull
            curbatch = -1;
            lastfcb = 0;
            count = 0;
            lastagg = 0;

            //click_chatter("Pull continue because received %d ! tail %d, head %d",batch->count(),tail,head);
            p = batch;
            goto process;
        }
        */
    }
}


/**
 * Checks that the FCB has not timed out
 */
static inline void check_fcb_still_valid(FlowControlBlock* fcb, Timestamp now) {
#if HAVE_CTX_GLOBAL_TIMEOUT
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
inline bool CTXManager::is_valid_fcb(Packet* &p, Packet* &last, Packet* &next, FlowControlBlock* &fcb, const Timestamp &now) {
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

inline FlowControlBlock* CTXManager::get_cache_fcb(Packet* p, uint32_t agg, FlowNode* root) {

    if (unlikely(agg == 0)) {
        return _table.match(p, root);
    }

#if DEBUG_CLASSIFIER > 1
    click_chatter("Aggregate %d",agg);
#endif
        FlowControlBlock* fcb = 0;
        uint16_t hash = (agg ^ (agg >> 16)) & _cache_mask;
#if USE_CACHE_RING
        FlowCache* bucket = _cache.get() + ((uint32_t)hash * _cache_ring_size);
#else
        FlowCache* bucket = _cache.get() + hash;
#endif
        FlowCache* c = bucket;
        int ic = 0;
        do {
            if (c->agg == 0) { //Empty slot
    #if DEBUG_CLASSIFIER > 1
                click_chatter("Cache miss !");
    #endif
                cache_miss++;
                return set_fcb_cache(c,p,agg);
            } else { //Non empty slot
                if (likely(c->agg == agg)) { //Good agg
                    if (likely(_collision_is_life || (c->fcb->parent && _table.reverse_match(c->fcb, p, _table.get_root())))) {
        #if DEBUG_CLASSIFIER > 1
                        click_chatter("Cache hit");
        #endif
                        cache_hit++;
                        fcb = c->fcb;
                        return fcb;
                        //OK
                    } else { //The fcb for that agg does not match !

                        cache_sharing++;
                        fcb = _table.match(p, root);

#if DEBUG_CLASSIFIER > 1 || DEBUG_FCB_CACHE

                        click_chatter("Cache %d shared for agg %d : fcb %p %p!",hash,agg,fcb,c->fcb);
                        flow_assert(fcb != c->fcb);
                        //for (int i = 0; i < 10; i++)
                            //click_chatter("%x",*(((uint32_t*)p->data()) + i));

#endif
                        return fcb;
                        /*FlowControlBlock* firstfcb = fcb;
                        int sechash = (agg >> 12) & 0xfff;
                        fcb = _cache.get()[sechash + 4096];
                        if (fcb && !fcb->released()) {
                            if (_table.reverse_match(fcb, p)) {

                            } else {
                                click_chatter("Double collision for hash %d!",hash);
                                fcb = _table.match(p);
                                if (_cache.get()[hash]->lastseen < fcb->lastseen)
                                    _cache.get()[hash] = fcb;
                                else
                                    _cache.get()[sechash] = fcb;
                            }
                        } else {
                            fcb = _table.match(p);
                            _cache.get()[sechash] = fcb;
                        }*/
                    }
                } //Continue if bad agg
            }
#if !USE_CACHE_RING
        } while (false);
        fcb = _table.match(p,root);
        if (fcb->dynamic > 1) {
            click_chatter("ADDING A DYNAMIC TWICE");
            table().get_root()->print();
            assert(false);
        }

        return set_fcb_cache(c,p, agg);
#else
            c++;
            ic++;
        } while (ic < _cache_ring_size); //Try to put in the ring of the bucket

        //Remove the oldest from the bucket
        c = bucket;
        FlowCache* oldest = c;
        c++;
        ic = 1;
        int o = 0;
        while (ic < _cache_ring_size) {
            if (c->fcb->lastseen < oldest->fcb->lastseen) {
                o = ic;
                oldest = c;
            }

            c++;
            ic++;
        }
        //click_chatter("Oldest is %d",o );
        c = bucket;
        if (o != 0) {
            oldest->agg = c->agg;
            oldest->fcb = c->fcb;
        }

        #if DEBUG_CLASSIFIER > 1
        click_chatter("Cache miss with full ring !");
        #endif
        cache_miss++;
        return set_fcb_cache(c,p, agg);
#endif
}



CLICK_ENDDECLS
#endif
