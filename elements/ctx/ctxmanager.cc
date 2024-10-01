/*
 * ctxmanager.{cc,hh} Context/Flow Manager
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/routervisitor.hh>
#include "ctxmanager.hh"
#include "ctxdispatcher.hh"
#include <click/idletask.hh>
#include <click/hashtable.hh>
#include <algorithm>
#include <set>
#include <click/flow/flowelement.hh>

CLICK_DECLS

CTXManager::CTXManager(): _aggcache(false), _cache(0), _cache_size(4096), _cache_ring_size(8), _pull_burst(0),
_builder(true), _collision_is_life(false), cache_hit(0), cache_miss(0), cache_sharing(0),
_clean_timer(5000), _timer(this), _early_drop(true),
    _ordered(true),_nocut(false), _optimize(true), Router::InitFuture(this) {
    in_batch_mode = BATCH_MODE_NEEDED;
#if DEBUG_CLASSIFIER
    _verbose = 3;
    _size_verbose = 3;
#else
    _verbose = 0;
    _size_verbose = 0;
#endif
}

CTXManager::~CTXManager() {

}

int
CTXManager::configure(Vector<String> &conf, ErrorHandler *errh)
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
#if HAVE_CTX_GLOBAL_TIMEOUT
            .read("CLEAN_TIMER",_clean_timer)
#endif
            .read("EARLYDROP", _early_drop)
            .read("ORDERED", _ordered) //Enforce FCB order of access
            .read("NOCUT", _nocut)
            .read("OPTIMIZE", _optimize)
#if HAVE_FLOW_DYNAMIC
            .read_or_set("RELEASE", _do_release, true)
#endif
            .complete() < 0)
        return -1;

    find_children(_verbose);

    //As all VirtualFlowManager, we must ensure the main future is called
    router()->get_root_init_future()->post(&_fcb_builded_init_future);

    //Once all CTXDispatchers have been initialized, we can initialize the CTXManager
    ctx_builded_init_future()->post(this);
    _fcb_builded_init_future.post(ctx_builded_init_future());
    _reserve = sizeof(FlowNodeData) + (_aggcache?sizeof(uint32_t):0) + reserve;

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

FlowNode* CTXManager::resolveContext(FlowType t, Vector<FlowElement*> contextStack) {
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

    return FlowClassificationTable::parse(this,prot).root;

}

#if HAVE_CTX_GLOBAL_TIMEOUT
void CTXManager::run_timer(Timer*) {
    debug_flow("Release timer!");
#if DEBUG_CLASSIFIER_RELEASE
    click_chatter("Force run check-release");
#endif
    fcb_table = &_table;
    _table.check_release();
    fcb_table = 0;
    if (_clean_timer > 0)
        _timer.reschedule_after_msec(_clean_timer);
}
#endif

void CTXManager::fcb_built() {
    _table.get_pool()->initialize(_reserve, &_table);
}

/**
 * Remove a fcb from the tree, deleting empty dynamic nodes in the parents
 * The FCB release itself is handled by the caller
 */
void release_subflow(FlowControlBlock* fcb, void* thunk) {
#if DEBUG_CLASSIFIER_RELEASE
    click_chatter("[%d] Release from tree fcb %p data %d, parent %p, thread %d",click_current_cpu_id(), fcb,fcb->data_64[0],fcb->parent, fcb->thread);
#endif

    flow_assert(thunk);
    CTXManager* fc = static_cast<CTXManager*>(thunk);
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
        flow_assert(parent->threads[click_current_cpu_id()]);
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


            if (child == parent->default_ptr()->ptr) { //Growing child was the default path
                debug_flow("Default");
                child->set_growing(false);
                child->destroy();
                parent->default_ptr()->ptr = subchild;
                subchild->set_parent(parent);
            } else { //Growing child of normal or growing
                debug_flow("Child");
                if (parent->growing()) { //Release growing of growing but unrelated as it is not the default
                    click_chatter("Growing of unrelated growing, not deleting");
                    break;
                } else { //Release growing of normal, we cannot swap pointer because find could give a different bucket
                    parent->release_child(FlowNodePtr(child), data); //A->release(B). As growing flag is set, the parent will not keep the pointer even if KEEP_STRUCTURE
                }
                //the child has been destroyed by release_child, because of the growing flag

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
            click_chatter("Parent has %d != %d counted children",parent->getNum(),parent->findGetNum());
            assert(false);
        }
#endif

        parent->check(true, false);
        child = parent; //Child is A
        data = child->node_data;
        parent = child->parent(); //Parent is 0
        up++;
    };
#if DEBUG_CLASSIFIER
    check_thread(parent, child);
#endif
    fc->table().get_root()->check();
}


int CTXManager::_initialize_classifier(ErrorHandler *errh) {
    if (input_is_pull(0)) {
        assert(input(0).element());
    }
    auto passing = get_passing_threads();
    _table.get_pool()->compress(passing);

    Vector<FlowElement*> s(1,0);
    s[0] = this;
    FlowNode* table = FlowElementVisitor::get_downward_table(this, 0, s);

    if (!table)
       return errh->error("%s: CTXManager without any downward dispatcher?",name().c_str());

    _table.set_release_fnt(release_subflow,this);
    if (table->is_dummy()) {
        return errh->error("%p{element} : CTXManager without classification !");
    }
    if (_verbose > 1) {
        click_chatter("Table of %s before optimization :",name().c_str());
        table->print(-1,false);
    }
    table->check();
    if (_optimize) {
        _table.set_root(table->optimize(passing));
    } else {
        _table.set_root(table);
    }
    _table.get_root()->check();
    if (_verbose) {
        click_chatter("Table of %s after optimization :",name().c_str());
        bool showptr = false;
#if DEBUG_CLASSIFIER
        showptr = true;
#endif
        _table.get_root()->print(-1,showptr);
    }
    FCBPool::initialized --;

    return 0;
}

/**
 * Replace all leafs of the tree by the final pool-alocated ones.
 * During initialization leafs have a double size to note which field are initialized.
 */
int CTXManager::_replace_leafs(ErrorHandler *errh) {
    //Replace FCBs by the final run-time ones
    //Also have common static FCBs
    HashTable<FlowControlBlockRef, FlowControlBlock*> known_static_fcbs;
    _table.get_root()->traverse_all_leaves([this,&known_static_fcbs](FlowNodePtr* ptr) {

        FlowControlBlock* nfcb;

        auto it = known_static_fcbs.find(FlowControlBlockRef(ptr->leaf));
        while (!ptr->parent()->level()->is_dynamic() && it) {
            nfcb = it->second;
            if (ptr->parent()->default_ptr()->leaf != ptr->leaf && !nfcb->get_data().equals(ptr->leaf->get_data())) {
                //The data is not the same, we need to change the FCB by a classification node with the right data
                //Change : this adds a classification step for a bit of memory. We don't care.
                ++it;
                continue;
                /*
                if (_verbose > 1) {
                    click_chatter("Need a new node to keep data");
                    ptr->leaf->print("");
                    nfcb->print("");
                }
                FlowNode* n = new FlowNodeDummy();
                n->threads = ptr->parent()->threads;
                n->set_parent(ptr->parent());
                n->set_level(new FlowLevelDummy());
                n->node_data = ptr->data();
                delete ptr->leaf;
                ptr->set_node(n);
                ptr = n->default_ptr();*/
            } else
                delete ptr->leaf;
            //Delete parent to specify this FCB has multiple parents
            if (nfcb->parent)
                nfcb->parent = 0;
            goto set;
         }

         {
            nfcb = _table.get_pool()->allocate();
            FlowNode* p = ptr->parent();
            memcpy(nfcb->data, ptr->leaf->data, _table.get_pool()->data_size());
            nfcb->parent = ptr->leaf->parent;
            nfcb->flags = ptr->leaf->flags;
#if DEBUG_CLASSIFIER
			if (nfcb->parent->threads.weight() == 1) {
				nfcb->thread = nfcb->parent->threads.clz();
			} else
				nfcb->thread = -1;
#endif
            known_static_fcbs.set(FlowControlBlockRef(nfcb), nfcb);
            assert(known_static_fcbs.find(FlowControlBlockRef(nfcb)));
            delete ptr->leaf;
        }
        set:
#if HAVE_FLOW_DYNAMIC
        nfcb->reset_count(1);
#endif
#if HAVE_FLOW_DYNAMIC
        nfcb->release_fnt = 0;
#endif
        ptr->set_leaf(nfcb);
    }, true, true);


    if (_verbose > 1) {
        click_chatter("Table of %s after replacing nodes :",name().c_str());
        bool showptr = false;
#if DEBUG_CLASSIFIER
        showptr = true;
#endif
        _table.get_root()->print(-1,showptr);
    }

    bool have_dynamic = false;

    _table.get_root()->traverse_all_nodes([this,&have_dynamic](FlowNode* n) {
            if (n->level()->is_dynamic()) {
                have_dynamic = true;
                return false;
            }
            return true;
    });
#if HAVE_VERBOSE_BATCH
    if (!have_dynamic && _do_release) {
        click_chatter("CTXManager is fully static, disabling release, consider compiling with --disable-dynamic-flow");
    }
#endif

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

/**
 * Initialize timeout timers
 */
int CTXManager::_initialize_timers(ErrorHandler *errh) {
    if (_do_release) {
#if HAVE_CTX_GLOBAL_TIMEOUT
        auto pushing = get_pushing_threads();
        for (unsigned i = 0; i < click_max_cpu_ids(); i++) {
            if (pushing[i]) {
                IdleTask* idletask = new IdleTask(this);
                idletask->initialize(this, i, 100);
            }
        }
        _timer.initialize(this);
        if (_clean_timer > 0)
            _timer.schedule_after_msec(_clean_timer);
#endif
    }

    return 0;
}

int CTXManager::solve_initialize(ErrorHandler *errh) {
    if (_initialize_classifier(errh) != 0)
        return -1;
    if (_replace_leafs(errh) != 0)
        return -1;
    if (_initialize_timers(errh) != 0)
        return -1;

    return Router::InitFuture::solve_initialize(errh);
}

void CTXManager::cleanup(CleanupStage stage) {
        fcb_table = &_table;
        bool previous = pool_allocator_mt_base::dying();
        pool_allocator_mt_base::set_dying(true);

        if (_table.get_root()) {
//            _table.get_root()->print();
//            click_chatter("Deleting!");
//
#if HAVE_CTX_GLOBAL_TIMEOUT
            //We are not on the right thread, so we'll delete the cache by ourselves
            //TODO : elements and cache assume release is done on the same thread, we must implement thread_cleanup
            //_table.delete_all_flows();
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
    //If aggcache is enabled, initialize the cache
    if (_aggcache && _cache_size > 0) {
        for (unsigned i = 0; i < _cache.weight(); i++) {
            if (_cache.get_value(i))
                CLICK_LFREE(_cache.get_value(i),sizeof(FlowCache) * _cache_size * _cache_ring_size);
        }
    }
    /*click_chatter("%p{element} Hit : %d",this,cache_hit);
    click_chatter("%p{element}  Shared : %d",this,cache_sharing);
    click_chatter("%p{element} Miss : %d",this,cache_miss);*/
}


bool CTXManager::run_idle_task(IdleTask*) {
    bool work_done = false;
#if HAVE_CTX_GLOBAL_TIMEOUT
//#if !HAVE_FLOW_DYNAMIC
    fcb_table = &_table;
//#endif
#if DEBUG_CLASSIFIER_TIMEOUT > 0
    click_chatter("%p{element} Idle release",this);
#endif
    work_done = _table.check_release();
//#if !HAVE_FLOW_DYNAMIC
    fcb_table = 0;
//#endif
#endif
    return work_done;
}

/**
 * Testing function
 * Search a FCB in the cache, linearly
 */
inline int CTXManager::cache_find(FlowControlBlock* fcb) {
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

/**
 * Remove a FCB from the cache
 */
inline void CTXManager::remove_cache_fcb(FlowControlBlock* fcb) {
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


/**
 * Push batch simple simply classify packets and push a batch when a packet
 * is different
 */
inline  void CTXManager::push_batch_simple(int port, PacketBatch* batch) {
    PacketBatch* awaiting_batch = NULL;
    Packet* p = batch->first();
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

/**
 * Push_batch_builder use double connection to build a ring and process packets trying to reconcile flows more
 *
 */
inline void CTXManager::push_batch_builder(int, PacketBatch* batch) {
    Packet* p = batch->first();
    FlowControlBlock* fcb = 0;
    uint32_t lastagg = 0;
    Packet* last = 0;
    Builder builder;

    Timestamp now = Timestamp::recent_steady();

    while (p != NULL) {
#if DEBUG_CLASSIFIER > 1
        click_chatter("Packet %p in %s",p,name().c_str());
#endif
        Packet* next = p->next();

        if (!get_fcb_for(p,fcb,lastagg,last,next,now))
            continue;

        handle_builder(p, last, fcb, builder, now);
        last = p;
        p = next;
    }

    flush_builder(last, builder, now);
    fcb_stack = 0;
    //click_chatter("End");

}


void CTXManager::push_batch(int, PacketBatch* batch) {
    FlowControlBlock* tmp_stack = fcb_stack;
    FlowTableHolder* tmp_table = fcb_table;
//#if !HAVE_FLOW_DYNAMIC
    fcb_table = &_table;
//#endif
    if (_builder)
        push_batch_builder(0,batch);
    else
        push_batch_simple(0,batch);

    check_release_flows();
    fcb_stack = tmp_stack;
//#if !HAVE_FLOW_DYNAMIC
    fcb_table = tmp_table;
//#endif
}

enum {h_leaves_count, h_active_count, h_print, h_timeout_count};
String CTXManager::read_handler(Element* e, void* thunk) {
    CTXManager* fc = static_cast<CTXManager*>(e);

    fcb_table = &fc->_table;
    switch ((intptr_t)thunk) {
        case h_active_count:
        case h_leaves_count: {
            int n = 0;
            fc->_table.get_root()->traverse_all_leaves([&n](FlowNodePtr* ptr) {
                #if HAVE_FLOW_DYNAMIC && FLOW_DEBUG_CLASSIFIER
                    click_chatter("%d",ptr->leaf->count());
                #endif
                n++;
            },true,(intptr_t)thunk==h_leaves_count);
            fcb_table = 0;
            return String(n);
        }
        case h_print:
            fc->_table.get_root()->print(-1,false,true,false);
            fcb_table = 0;
            return String("");
#if HAVE_CTX_GLOBAL_TIMEOUT
        case h_timeout_count:
            return String(fc->_table.old_flows->count());
#endif
        default:
            return String("<unknown>");
    }
};

void CTXManager::add_handlers() {
    add_read_handler("leaves_count", CTXManager::read_handler, h_leaves_count);
    add_read_handler("leaves_all_count", CTXManager::read_handler, h_leaves_count);
    add_read_handler("leaves_nondefault_count", CTXManager::read_handler, h_active_count);
    add_read_handler("print_tree", CTXManager::read_handler, h_print);
    add_read_handler("timeout_count", CTXManager::read_handler, h_timeout_count);
}

//int FlowBufferVisitor::shared_position[NR_SHARED_FLOW] = {-1};

CLICK_ENDDECLS
ELEMENT_REQUIRES(flow ctx)
EXPORT_ELEMENT(CTXManager)
ELEMENT_MT_SAFE(CTXManager)
