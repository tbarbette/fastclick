/*
 * flowclassifier.{cc,hh}
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/routervisitor.hh>
#include <click/flowelement.hh>
#include "flowclassifier.hh"
#include "flowdispatcher.hh"
#include <click/idletask.hh>
#include <click/hashtable.hh>
#include <algorithm>
#include <set>

CLICK_DECLS

FlowClassifier::FlowClassifier(): _aggcache(false), _cache(),_cache_size(4096), _cache_ring_size(8),_pull_burst(0),_builder(true),_collision_is_life(false), cache_miss(0),cache_sharing(0),cache_hit(0),_clean_timer(5000), _timer(this), _early_drop(true),
    _ordered(true),_nocut(false) {
    in_batch_mode = BATCH_MODE_NEEDED;
#if DEBUG_CLASSIFIER
    _verbose = 3;
    _size_verbose = 3;
#else
    _verbose = 0;
    _size_verbose = 0;
#endif
    FlowClassifier::_n_classifiers++;
}

FlowClassifier::~FlowClassifier() {

}

int
FlowClassifier::configure(Vector<String> &conf, ErrorHandler *errh)
{
    int reserve = 0;
    String context = "ETHER";

    if (Args(conf, this, errh)
            .read_p("AGGCACHE",_aggcache)
            .read_p("RESERVE",reserve)
            .read("CACHESIZE", _cache_size)
            .read("CACHERINGSIZE", _cache_ring_size)
            .read("BUILDER",_builder)
            .read("AGGTRUST",_collision_is_life)
            .read("VERBOSE",_verbose)
            .read("VERBOSE_FCB", _size_verbose)
            .read("CONTEXT",context)
#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
            .read("CLEAN_TIMER",_clean_timer)
#endif
            .read("EARLYDROP",_early_drop)
            .read("ORDERED", _ordered) //Enforce FCB order of access
            .read("NOCUT", _nocut)
            .complete() < 0)
        return -1;

    _reserve = sizeof(FlowNodeData) + (_aggcache?sizeof(uint32_t):0) + reserve;
    build_fcb();


    _cache_mask = _cache_size - 1;
    if ((_cache_size & _cache_mask) != 0)
        return errh->error("Chache size must be a power of 2 !");

    if (context == "ETHER") {
        _context = FLOW_ETHER;
    } else if (context == "NONE") {
        _context = FLOW_NONE;
    } else
        return errh->error("Invalid context %s !",context.c_str());

    return 0;
}

FlowNode* FlowClassifier::resolveContext(FlowType t, Vector<FlowElement*> contextStack) {
    String prot;
    if (_context == FLOW_ETHER) {
        switch (t) {
            case FLOW_IP:
                prot = "12/0800!";
                break;
            case FLOW_ARP:
                prot = "12/0806!";
                break;
            default:
                return FlowElement::resolveContext(t, contextStack);
        }
    } else {
        return FlowElement::resolveContext(t, contextStack);
    }

    return FlowClassificationTable::parse(prot).root;

}

#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
void FlowClassifier::run_timer(Timer*) {
    click_chatter("Release timer!");
#if DEBUG_CLASSIFIER_RELEASE
    click_chatter("Force run check-release");
#endif
    fcb_table = &_table;
    _table.check_release();
    fcb_table = 0;
    _timer.reschedule_after_msec(_clean_timer);
}
#endif



/**
 * Remove a fcb from the tree, deleting empty dynamic nodes in the parents
 * The FCB release itself is handled by the caller
 */
void release_subflow(FlowControlBlock* fcb, void* thunk) {
#if DEBUG_CLASSIFIER_RELEASE
    click_chatter("Release from tree fcb %p data %d, parent %p",fcb,fcb->data_64[0],fcb->parent);
#endif

    flow_assert(thunk);
    FlowClassifier* fc = static_cast<FlowClassifier*>(thunk);
#if DEBUG_CLASSIFIER
    if (fcb->parent != fc->table().get_root() && (!fcb->parent || !fc->table().get_root()->find_node(fcb->parent))) {
        click_chatter("GOING TO CRASH WHILE REMOVING fcb %p, with parent %p", fcb, fcb->parent);
        click_chatter("Parent exists : %d",fc->table().get_root()->find_node(fcb->parent));
        assert(false);
    }
#endif
    //A -> B -> F

    //Parent of the fcb is release_ptr
    if (fc->is_dynamic_cache_enabled())
        fc->remove_cache_fcb(fcb);
    FlowNode* child = static_cast<FlowNode*>(fcb->parent); //Child is B
    flow_assert(child->getNum() == child->findGetNum());
    flow_assert(fcb->parent);
    FlowNodeData data = *fcb->node_data;
    child->release_child(FlowNodePtr(fcb), data); //B->release(F)
#if DEBUG_CLASSIFIER_RELEASE
    fcb->parent = 0;
#endif
    flow_assert(child->getNum() == child->findGetNum());
    data = child->node_data; //Data is B data inside A

    FlowNode* parent = child->parent(); //Parent is A
    //If parent is dynamic, we cannot be the child of a default
    //Release nodes up to the root
    int up = 0;
    while (parent && parent->level()->is_dynamic() && child->getNum() == 0) { //A && B is empty and A is dynamic (so B was a duplicate as it comes from a FCB)
#if DEBUG_CLASSIFIER_RELEASE
        click_chatter("[%d] Releasing parent %s's child %p, growing %d, num %d, child is type %s num %d",up,parent->name().c_str(), child, parent->growing(), parent->getNum(),child->name().c_str(),child->getNum());
#endif
        parent->check(true, false);
        flow_assert(child->level()->is_dynamic());
        if (parent->growing() && !child->growing() && child == parent->default_ptr()->ptr) {
            //If child is the non-growing default path of a growing parent, it is the default child table of a growing table and must not be deleted
            //click_chatter("Non growing child of its growing original, not deleting");

            flow_assert(parent->getNum() == parent->findGetNum());
            break;
        }
        if (child->growing()) {
            debug_flow("Releasing a growing child, we can remove it from the tree. Parent num is %d",parent->getNum());
            flow_assert(parent->getNum() == parent->findGetNum());
            FlowNode* subchild = child->default_ptr()->node;

            //Clean the growing flag for fast pool reuse
            child->set_growing(false);
            if (child == parent->default_ptr()->ptr) { //Growing child was the default path
                debug_flow("Default");
                child->destroy();
                parent->default_ptr()->ptr = subchild;
                subchild->set_parent(parent);
            } else { //Growing child of normal or growing
                debug_flow("Child");
                if (parent->growing()) { //Release growing of growing but unrelated as it is not the default
                    click_chatter("Growing of unrelated growing, not deleting");
                    break;
                } else { //Release growing of normal, we cannot swap pointer because find could give a different bucket
                    parent->release_child(FlowNodePtr(child), data); //A->release(B)
                }

                bool need_grow;
                parent->find(data,need_grow)->set_node(subchild);
                subchild->set_parent(parent);
                subchild->node_data = data;
                parent->inc_num();
                break; //No need to continue, parent now has a child
            }
        } else { //Child is not growing, we remove a normal child
            debug_flow_2("Non-growing");
            flow_assert(parent->getNum() == parent->findGetNum());
            parent->release_child(FlowNodePtr(child), data); //A->release(B)
        }
#if DEBUG_CLASSIFIER_CHECK || DEBUG_CLASSIFIER
        if (parent->getNum() != parent->findGetNum()) {
            click_chatter("Parent has %d != %d counted childs",parent->getNum(),parent->findGetNum());
            assert(false);
        }
#endif

        parent->check(true, false);
        child = parent; //Child is A
        data = child->node_data;
        parent = child->parent(); //Parent is 0
        up++;
    };
}


int FlowClassifier::_initialize_classifier(ErrorHandler *errh) {
    if (input_is_pull(0)) {
        assert(input(0).element());
    }
    auto passing = get_passing_threads();
    _table.get_pool()->compress(passing);

    Vector<FlowElement*> s(1,0);
    s[0] = this;
    FlowNode* table = FlowElementVisitor::get_downward_table(this, 0, s);

    if (!table)
       return errh->error("%s: FlowClassifier without any downward dispatcher?",name().c_str());

    _table.set_release_fnt(release_subflow,this);
    if (table->is_dummy()) {
        return errh->error("%p{element} : FlowClassifier without classification !");
    }
    _table.set_root(table->optimize(passing.weight() <= 1));
    _table.get_root()->check();
    if (_verbose) {
        click_chatter("Table of %s after optimization :",name().c_str());
        _table.get_root()->print(-1,false);
    }
    FCBPool::initialized --;
    return 0;
}

int FlowClassifier::_replace_leafs(ErrorHandler *errh) {
    //Replace FCBs by the final run-time ones
    //Also have common static FCBs
    HashTable<FlowControlBlockRef, FlowControlBlock*> known_static_fcbs;
    _table.get_root()->traverse_all_leaves([this,&known_static_fcbs](FlowNodePtr* ptr) {

        FlowControlBlock* nfcb;

        auto it = known_static_fcbs.find(FlowControlBlockRef(ptr->leaf));
        if (!ptr->parent()->level()->is_dynamic() && it) {
            nfcb = it->second;
            if (ptr->parent()->default_ptr()->leaf != ptr->leaf && !nfcb->get_data().equals(ptr->leaf->get_data())) {
                //The data is not the same, we need to change the FCB by a classification node with the right data
                click_chatter("Need a new node to keep data");
                ptr->leaf->print("");
                nfcb->print("");
                FlowNode* n = new FlowNodeDummy();
                n->set_parent(ptr->parent());
                n->set_level(new FlowLevelDummy());
                n->node_data = ptr->data();
                delete ptr->leaf;
                ptr->set_node(n);
                ptr = n->default_ptr();
            } else
                delete ptr->leaf;
            //Delete parent to specify this FCB has multiple parents
            if (nfcb->parent)
                nfcb->parent = 0;
         } else {
            nfcb = _table.get_pool()->allocate();
            FlowNode* p = ptr->parent();
            memcpy(nfcb->data, ptr->leaf->data, _table.get_pool()->data_size());
            nfcb->parent = ptr->leaf->parent;
            nfcb->flags = ptr->leaf->flags;
            known_static_fcbs.set(FlowControlBlockRef(nfcb), nfcb);
            assert(known_static_fcbs.find(FlowControlBlockRef(nfcb)));
            delete ptr->leaf;
        }
#if HAVE_FLOW_DYNAMIC
        nfcb->acquire(1);
#endif
        nfcb->release_fnt = 0;
        ptr->set_leaf(nfcb);
    }, true, true);

    if (_verbose) {
        click_chatter("Table of %s after replacing nodes :",name().c_str());
        _table.get_root()->print(-1,false);
    }


    bool have_dynamic = false;

    _table.get_root()->traverse_all_nodes([this,&have_dynamic](FlowNode* n) {
            if (n->level()->is_dynamic()) {
                have_dynamic = true;
                return false;
            }
            return true;
    });
    if (!have_dynamic && _do_release) {
        click_chatter("FlowClassifier is fully static, disabling release, consider using FlowClassifierStatic");
    }

#ifndef HAVE_FLOW_DYNAMIC
    if (have_dynamic) {
        return errh->error("You have dynamic flow elements, but this was build without dynamic support. Rebuild with --enable-flow-dynamic.");
    }
#endif
    //If aggcache is enabled, initialize the cache
    if (_aggcache && _cache_size > 0) {
        for (unsigned i = 0; i < _cache.weight(); i++) {
            _cache.get_value(i) = (FlowCache*)CLICK_LALLOC(sizeof(FlowCache) * _cache_size * _cache_ring_size);
            bzero(_cache.get_value(i), sizeof(FlowCache) * _cache_size * _cache_ring_size);
        }
    }
    return 0;
}

int FlowClassifier::_initialize_timers(ErrorHandler *errh) {
    if (_do_release) {
#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
        for (unsigned i = 0; i < click_max_cpu_ids(); i++) {
            IdleTask* idletask = new IdleTask(this);
            idletask->initialize(this, i, 100);
        }
        //todo : INIT timer if needed? The current solution seems ok
#endif
    }

    assert(one_upstream_classifier() != this);
    return 0;
}

int FlowClassifier::initialize(ErrorHandler *errh) {
    if (_initialize_classifier(errh) != 0)
        return -1;
    if (_replace_leafs(errh) != 0)
        return -1;
    if (_initialize_timers(errh) != 0)
        return -1;
    return 0;
}

void FlowClassifier::cleanup(CleanupStage stage) {
        fcb_table = &_table;
        bool previous = pool_allocator_mt_base::dying();
        pool_allocator_mt_base::set_dying(true);

        if (_table.get_root()) {
//            _table.get_root()->print();
//            click_chatter("Deleting!");
//
#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
            _table.delete_all_flows();
#endif
//            _table.get_root()->print();
        }
        pool_allocator_mt_base::set_dying(previous);
    if (_table.get_root()) {
        _table.get_root()->traverse_all_leaves([this](FlowNodePtr* ptr) {
            _table.get_pool()->release(ptr->leaf);
            ptr->leaf = 0;
        }, true, true);
    }
    fcb_table = 0;
    /*click_chatter("%p{element} Hit : %d",this,cache_hit);
    click_chatter("%p{element}  Shared : %d",this,cache_sharing);
    click_chatter("%p{element} Miss : %d",this,cache_miss);*/
}

#define USE_CACHE_RING 1

bool FlowClassifier::run_idle_task(IdleTask*) {
#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
//#if !HAVE_DYNAMIC_FLOW_RELEASE_FNT
    fcb_table = &_table;
//#endif
#if DEBUG_CLASSIFIER_TIMEOUT > 0
    click_chatter("%p{element} Idle release",this);
#endif
    bool work_done = _table.check_release();
//#if !HAVE_DYNAMIC_FLOW_RELEASE_FNT
    fcb_table = 0;
//#endif
    //return work_done;
#endif
    return false; //No reschedule
}

/**
 * Testing function
 */
inline int FlowClassifier::cache_find(FlowControlBlock* fcb) {
    if (!_aggcache)
        return 0;
    FlowCache* bucket = _cache.get();
    int n = 0;
    int i = 0;
    while (bucket < _cache.get() + (_cache_size * _cache_ring_size)) {
        if (bucket->agg != 0 && bucket->fcb == fcb) {
            n++;
        }
        bucket++;
        i++;
    }
    return n;
}

inline void FlowClassifier::remove_cache_fcb(FlowControlBlock* fcb) {
    uint32_t agg = *((uint32_t*)&(fcb->node_data[1]));
    if (agg == 0)
        return;
    uint16_t hash = (agg ^ (agg >> 16)) & _cache_mask;
#if USE_CACHE_RING
        FlowCache* bucket = _cache.get() + ((uint32_t)hash * _cache_ring_size);
        FlowCache* c = bucket;
        int ic = 0;
        do {
            if (c->agg == agg && c->fcb == fcb) {
                FlowCache* bxch = bucket + _cache_ring_size - 1;
                while (bxch->agg == 0) {
                    bxch--;
                }
                c->agg = bxch->agg;
                c->fcb = bxch->fcb;
                bxch->agg = 0;
                return;
            }
            ic++;
            c++;
        } while (ic < _cache_ring_size);
        click_chatter("REMOVING a FCB from the cache that was not in the cache %p, agg %u",fcb, agg);
#else
        FlowCache* bucket = _cache.get() + hash;
        if (bucket->agg == agg) {
            bucket->agg = 0;
        }
#endif

}

inline FlowControlBlock* FlowClassifier::set_fcb_cache(FlowCache* &c, Packet* &p, const uint32_t& agg) {
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

inline FlowControlBlock* FlowClassifier::get_cache_fcb(Packet* p, uint32_t agg) {

    if (unlikely(agg == 0)) {
        return _table.match(p);
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
                    if (likely(_collision_is_life || (c->fcb->parent && _table.reverse_match(c->fcb, p)))) {
        #if DEBUG_CLASSIFIER > 1
                        click_chatter("Cache hit");
        #endif
                        cache_hit++;
                        fcb = c->fcb;
                        return fcb;
                        //OK
                    } else { //The fcb for that agg does not match !

                        cache_sharing++;
                        fcb = _table.match(p);

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
        fcb = _table.match(p);
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

inline bool FlowClassifier::get_fcb_for(Packet* &p, FlowControlBlock* &fcb, uint32_t &lastagg, Packet* &last, Packet* &next, Timestamp &now) {
    if (_aggcache) {
        uint32_t agg = AGGREGATE_ANNO(p);
        if (!(lastagg == agg && fcb && likely(fcb->parent && _table.reverse_match(fcb,p)))) {
            if (_cache_size > 0)
                fcb = get_cache_fcb(p,agg);
            else
                fcb = _table.match(p);
            lastagg = agg;
        }
    }
    else
    {
        fcb = _table.match(p);
    }
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

/**
 * Push batch simple simply classify packets and push a batch when a packet
 * is different
 */
inline  void FlowClassifier::push_batch_simple(int port, PacketBatch* batch) {
    PacketBatch* awaiting_batch = NULL;
    Packet* p = batch;
    Packet* last = NULL;
    uint32_t lastagg = 0;
    Timestamp now = Timestamp::recent_steady();

    int count =0;
    FlowControlBlock* fcb = 0;
#if DEBUG_CLASSIFIER > 1
    click_chatter("Simple have %d packets.",batch->count());
#endif
    while (p != NULL) {
#if DEBUG_CLASSIFIER > 1
        click_chatter("Packet %p in %s",p,name().c_str());
#endif
        Packet* next = p->next();

        if (!get_fcb_for(p,fcb,lastagg,last,next,now)) {
            continue;
        }

        handle_simple(p, last, fcb, awaiting_batch, count, now);

        last = p;
        p = next;
    }

    flush_simple(last, awaiting_batch, count,  now);
}



#define RING_SIZE 16

/**
 * Push_batch_builder use double connection to build a ring and process packets trying to reconcile flows more
 *
 */
inline void FlowClassifier::push_batch_builder(int, PacketBatch* batch) {
    Packet* p = batch;
    int curbatch = -1;
    Packet* last = NULL;
    FlowControlBlock* fcb = 0;
    FlowControlBlock* lastfcb = 0;
    uint32_t lastagg = 0;
    int count = 0;

    FlowBatch batches[RING_SIZE];
    int head = 0;
    int tail = 0;

    Timestamp now = Timestamp::recent_steady();
    process:


    //click_chatter("Builder have %d packets.",batch->count());

    while (p != NULL) {
#if DEBUG_CLASSIFIER > 1
        click_chatter("Packet %p in %s",p,name().c_str());
#endif
        Packet* next = p->next();

        if (!get_fcb_for(p,fcb,lastagg,last,next,now))
            continue;
        if ((_nocut && lastfcb) || lastfcb == fcb) {
            //Just continue as they are still linked
        } else {

                //Break the last flow
                if (last) {
                    last->set_next(0);
                    batches[curbatch].batch->set_count(count);
                    batches[curbatch].batch->set_tail(last);
                }

                //Find a potential match
                for (int i = tail; i < head; i++) {
                    if (batches[i % RING_SIZE].fcb == fcb) { //Flow already in list, append
                        //click_chatter("Flow already in list");
                        curbatch = i % RING_SIZE;
                        count = batches[curbatch].batch->count();
                        last = batches[curbatch].batch->tail();
                        last->set_next(p);
                        goto attach;
                    }
                }
                //click_chatter("Unknown fcb %p, curbatch = %d",fcb,head);
                curbatch = head % RING_SIZE;
                head++;

                if (tail % RING_SIZE == head % RING_SIZE) {
                    auto &b = batches[tail % RING_SIZE];
                    if (_verbose > 1) {
                        click_chatter("WARNING (unoptimized) Ring full with batch of %d packets, processing now !", b.batch->count());
                    }
                    //Ring full, process batch NOW
                    fcb_stack = b.fcb;
#if HAVE_FLOW_DYNAMIC
                    fcb_stack->acquire(b.batch->count());
#endif
                    fcb_stack->lastseen = now;
                    //click_chatter("FPush %d of %d packets",tail % RING_SIZE,batches[tail % RING_SIZE].batch->count());
                    output_push_batch(0,b.batch);
                    tail++;
                }
                //click_chatter("batches[%d].batch = %p",curbatch,batch);
                //click_chatter("batches[%d].fcb = %p",curbatch,fcb);
                batches[curbatch].batch = PacketBatch::start_head(p);
                batches[curbatch].fcb = fcb;
                count = 0;
        }
        attach:
        count ++;
        last = p;
        p = next;
        lastfcb = fcb;
    }


    if (last) {
        last->set_next(0);
        batches[curbatch].batch->set_tail(last);
        batches[curbatch].batch->set_count(count);
    }

    //click_chatter("%d batches :",head-tail);
    for (int i = tail;i < head;i++) {
        //click_chatter("[%d] %d packets for fcb %p",i,batches[i%RING_SIZE].batch->count(),batches[i%RING_SIZE].fcb);
    }
    for (;tail < head;) {
        auto &b = batches[tail % RING_SIZE];
        fcb_stack = b.fcb;

#if HAVE_FLOW_DYNAMIC
        fcb_stack->acquire(b.batch->count());
#endif
        fcb_stack->lastseen = now;
        //click_chatter("EPush %d of %d packets",tail % RING_SIZE,batches[tail % RING_SIZE].batch->count());
        output_push_batch(0,b.batch);

        curbatch = -1;
        lastfcb = 0;
        count = 0;
        lastagg = 0;

        tail++;
        if (tail == head) break;

        if (input_is_pull(0) && ((batch = input_pull_batch(0,_pull_burst)) != 0)) { //If we can pull and it's not the last pull
            //click_chatter("Pull continue because received %d ! tail %d, head %d",batch->count(),tail,head);
            p = batch;
            goto process;
        }
    }
    fcb_stack = 0;
    //click_chatter("End");

}


void FlowClassifier::push_batch(int, PacketBatch* batch) {
    FlowControlBlock* tmp_stack = fcb_stack;
    FlowTableHolder* tmp_table = fcb_table;
//#if !HAVE_DYNAMIC_FLOW_RELEASE_FNT
    fcb_table = &_table;
//#endif
    if (_builder)
        push_batch_builder(0,batch);
    else
        push_batch_simple(0,batch);

    if (_do_release) {
#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
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
    fcb_stack = tmp_stack;
//#if !HAVE_DYNAMIC_FLOW_RELEASE_FNT
    fcb_table = tmp_table;
//#endif
}

String FlowClassifier::read_handler(Element* e, void* thunk) {
    FlowClassifier* fc = static_cast<FlowClassifier*>(e);

    fcb_table = &fc->_table;
    switch ((intptr_t)thunk) {
        case 0: {
            int n = 0;
            fc->_table.get_root()->traverse_all_leaves([&n](FlowNodePtr* ptr) {
                #if HAVE_FLOW_DYNAMIC
                click_chatter("%d",ptr->leaf->count());
                #endif
                n++;
            },true,true);
            fcb_table = 0;
            return String(n);
        }
        case 1:
            fc->_table.get_root()->print(-1,false);
            fcb_table = 0;
            return String("");
        default:
            return String("<unknown>");
    }
};

void FlowClassifier::add_handlers() {
    add_read_handler("leaves", FlowClassifier::read_handler, 0);
    add_read_handler("print_tree", FlowClassifier::read_handler, 1);
}


class ElementDistanceCastTracker : public RouterVisitor { public:

    typedef Pair<Element*,int> EDPair;


     ElementDistanceCastTracker(Router *router, bool stopAtFirst = true);

     /** @brief Return the elements that matched. */
     const Vector<EDPair> &elements() const {
         return _elements;
     }

     /** @brief Add element @a e to the set of matching elements. */
     void insert(Element *e, int distance);

     /** @brief Clear the set of matching elements. */
     void clear() {
         _reached.clear();
         _elements.clear();
     }

   private:

    bool visit(Element *e, bool isoutput, int port,
           Element *from_e, int from_port, int distance);
    int distance(Element *e, Element *from_e) {
        if (from_e && from_e->cast("VirtualFlowSpaceElement")) {
           click_chatter("Distance to VFSE %p{element}: ",e, dynamic_cast<VirtualFlowSpaceElement*>(from_e)->flow_data_size());
           return dynamic_cast<VirtualFlowSpaceElement*>(from_e)->flow_data_size();
        }
        return 0;
    }
    Bitvector _reached;
    Vector<EDPair> _elements;
    bool _continue;

};


ElementDistanceCastTracker::ElementDistanceCastTracker(Router *router, bool stopAtFirst)
    :  _reached(router->nelements(), false), _continue(!stopAtFirst) {
}

void
ElementDistanceCastTracker::insert(Element *e, int distance) {
    if (!_reached[e->eindex()]) {
        _reached[e->eindex()] = true;
        _elements.push_back(EDPair(e,distance));
    } else {
        for (int i = 0; i < _elements.size(); i++)
            if (_elements[i].first == e) {
                if (_elements[i].second < distance)
                    _elements[i].second = distance;
                break;
            }
    }
}

bool
ElementDistanceCastTracker::visit(Element *e, bool, int,
                  Element *, int, int distance)
{
    FlowElement* fe = dynamic_cast<FlowElement*>(e);
    if (fe && fe->stopClassifier())
        return false;
    if (e->cast("VirtualFlowSpaceElement")) {
        if (dynamic_cast<VirtualFlowSpaceElement*>(fe)->flow_data_size() > 0)
            insert(e,distance);
        return _continue;
    } else
        return true;
}

class UnstackVisitor : public RouterVisitor {
public:
    bool visit(Element *e, bool isoutput, int port,
                   Element *from_e, int from_port, int distance) {
        FlowElement* fe = dynamic_cast<FlowElement*>(e);

        if (fe && fe->stopClassifier())
            return false;

        VirtualFlowSpaceElement* fbe = dynamic_cast<VirtualFlowSpaceElement*>(e);
        if (fbe == NULL) {

            const char *f = e->router()->flow_code_override(e->eindex());
            if (!f)
                f = e->flow_code();
            if (strcmp(f,Element::COMPLETE_FLOW) != 0) {
#if DEBUG_CLASSIFIER > 0
                click_chatter("%p{element}: Unstacking flows from port %d", e,
port);
#endif
                const_cast<Element::Port&>(from_e->port(true,from_port)).set_unstack(true);
                return false;
            }
        }

        return true;
    }
};


struct el {
    int id;
    int count;
    int distance;
};

bool cmp(el a, el b) {
    return a.count > b.count || (a.count==b.count &&  a.distance < b.distance);
}

void
FlowClassifier::build_fcb() {
    //Find list of reachable elements
    ElementDistanceCastTracker reachables(router(),false);
    router()->visit(this, true, -1, &reachables);

    if (_size_verbose > 0) {
        click_chatter("Reachable VirtualFlowSpaceElement element list :");
        for (int i = 0; i < reachables.elements().size(); i++) {
            click_chatter("Reachable from %p{element} : %p{element}, max offset %d",this,reachables.elements()[i].first,reachables.elements()[i].second);
        }
    }

    _reachable_list = reachables.elements();
    _classifiers.push_back(this);
    //If last of the classifiers
    if (_classifiers.size() == _n_classifiers) {
        if (_size_verbose > 0)
            click_chatter("Last classifier");

        typedef Pair<int,int> CountDistancePair;
        HashTable<int,CountDistancePair> common(CountDistancePair{0,INT_MAX});
        int min_place = 0;

        //Counting elements that appear multiple times and their minimal distance
        for (int i = 0; i < _classifiers.size(); i++) {
            FlowClassifier* fc = dynamic_cast<FlowClassifier*>(_classifiers[i]);
            if (fc->_reserve > min_place)
                min_place = fc->_reserve;
            for (int j = 0; j < _classifiers[i]->_reachable_list.size(); j++) {
                auto ptr = common.find_insert(_classifiers[i]->_reachable_list[j].first->eindex(),CountDistancePair(0,_classifiers[i]->_reachable_list[j].second));
                ptr->second.first++;
                if (ptr->second.second > _classifiers[i]->_reachable_list[j].second)
                    ptr->second.second = _classifiers[i]->_reachable_list[j].second;
            }

            //Set unstack before non-compatible element
            UnstackVisitor uv = UnstackVisitor();
            router()->visit_ports(_classifiers[i], true, -1, &uv);
        }

        //Placing elements in a vector to sort them
        Vector<el> elements;
        for (auto it = common.begin(); it != common.end(); it++) {
            //click_chatter("%p{element} -> %d,%d ",router()->element(it->first),it->second.first,it->second.second);
            elements.push_back(el{it->first,it->second.first, it->second.second});
        }

        //Sorting the element, so we place shared first
        std::sort(elements.begin(), elements.end(),cmp);

        //We now place all elements
        std::set<int> already_placed;
        for (auto it = elements.begin(); it != elements.end(); it++) {
            VirtualFlowSpaceElement* e = dynamic_cast<VirtualFlowSpaceElement*>(router()->element(it->id));
            int my_place;
            if (_ordered)
                my_place = min_place + it->distance;
            else
                my_place = min_place;
            Bitvector v(false);
            //For each already placed element that are reachable from this one, we set the assigned bits in the vector
            for (auto ai = already_placed.begin(); ai != already_placed.end(); ai++) {
                int aid = *ai;
                VirtualFlowSpaceElement* ae = dynamic_cast<VirtualFlowSpaceElement*>(router()->element(aid));
                if (router()->element_can_reach(e,ae)) {
                    if (v.size() < ae->flow_data_offset() + ae->flow_data_size())
                        v.resize(ae->flow_data_offset() + ae->flow_data_size());
                    v.set_range(ae->flow_data_offset(), ae->flow_data_size(), true);
                }
            }

            while (v.range(my_place,e->flow_data_size())) {
                my_place++;
            }

            if (_size_verbose > 0)
                click_chatter("Placing  %p{element} at [%d-%d]",e,my_place,my_place + e->flow_data_size() -1 );
            already_placed.insert(it->id);
            e->_flow_data_offset = my_place;
            e->_classifier = dynamic_cast<FlowClassifier*>(_classifiers[0]);
        }

        //Set pool data size for classifiers
        for (int i = 0; i < _classifiers.size(); i++) {
            FlowClassifier* fc = _classifiers[i];
            fc->_pool_data_size = min_place;
            for (int j = 0; j < fc->_reachable_list.size(); j++) {
                VirtualFlowSpaceElement* vfe = dynamic_cast<VirtualFlowSpaceElement*>(fc->_reachable_list[j].first);
                int tot = vfe->flow_data_offset() + vfe->flow_data_size();
                if (tot > fc->_pool_data_size)
                    fc->_pool_data_size = tot;
            }
            fc->_table.get_pool()->initialize(fc->_pool_data_size, &fc->_table);
        }
    }
}

//int FlowBufferVisitor::shared_position[NR_SHARED_FLOW] = {-1};
int FlowClassifier::_n_classifiers = 0;
Vector<FlowClassifier *> FlowClassifier::_classifiers;

CLICK_ENDDECLS
ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(FlowClassifier)
ELEMENT_MT_SAFE(FlowClassifier)
