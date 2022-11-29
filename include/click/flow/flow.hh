#ifndef CLICK_FLOW
#define CLICK_FLOW 1

#include <click/timestamp.hh>
#include <click/error.hh>
#include <click/sync.hh>
#include <click/packet.hh>
#include <click/multithread.hh>
#include <click/bitvector.hh>
#include <click/vector.hh>
#include <click/list.hh>
#include <click/allocator.hh>

#include <thread>
#include "common.hh"

CLICK_DECLS

#ifdef HAVE_FLOW

#include "node/flow_nodes.hh"

inline void check_thread(FlowNode* parent, FlowNode* child) {
#if !DEBUG_CLASSIFIER
    assert(false); //Never run this in non-debug
#endif
    FlowNodeData data = child->node_data;
    while (parent && dynamic_cast<FlowLevelThread*>(parent->level()) == 0) {
        child = parent;
        data = child->node_data;
        parent = parent->parent();
    }
    /*
        click_chatter("Releasing a nonthread??");
        parent->print();
        abort();
    }*/
    if (!parent)
	return;
    if (data.data_32 != click_current_cpu_id()) {
        click_chatter("Thead %d in level of thread %d", click_current_cpu_id(), data.data_32);
        assert(false);
    }
}


class FlowClassificationTable : public FlowTableHolder {
public:
    FlowClassificationTable();
    ~FlowClassificationTable();

    void set_root(FlowNode* node);
    FlowNode* get_root();

    inline FlowControlBlock* match(Packet* p, FlowNode* parent);

    inline FlowControlBlock* match(Packet* p) {
        return match(p, _root);
    }
    inline bool reverse_match(FlowControlBlock* sfcb, Packet* p, FlowNode* root);

    typedef struct {
        FlowNode* root;
        int output;
        bool is_default;
    } Rule;
    static Rule parse(Element* owner, String s, bool verbose = false, bool add_leaf=true);
    static Rule make_drop_rule(bool ed = false) {
        Rule r = parse(0, "- drop");
        if (ed)
            r.root->traverse_all_leaves([](FlowNodePtr* ptr){ ptr->leaf->set_early_drop();}, true, true);
        return r;
    }
    static Rule make_ip_mask(IPAddress dst, IPAddress mask);
protected:
    FlowNode* _root;
};


/*****************************************************
 * Inline implementations for performances
 *****************************************************/

/**
 * Check that a SFCB match correspond to a packet
 */
bool FlowClassificationTable::reverse_match(FlowControlBlock* sfcb, Packet* p, FlowNode* root) {
#if HAVE_FLOW_EXACT_MATCH
    //TODO : work in progress
    __m256i pdata =  _mm256_loadu_si256((__m128i*) p->data());
    __m256i cdata =  _mm256_loadu_si256((__m128i*) sfcb->val);
    __m256i mask =  _mm256_loadu_si256((__m128i*) *sfcb->mask);
    pdata = _mm256_and_si256(pdata, mask);
    cdata = _mm256_and_si256(cdata, mask);
    return _mm256_testc_si256(pdata, cdata);
#endif

    FlowNode* parent = (FlowNode*)sfcb->parent;
#if DEBUG_CLASSIFIER
    if (parent != root && (!parent || !root->find_node(parent))) {
        click_chatter("GOING TO CRASH WHILE MATCHING fcb %p, with parent %p", sfcb, parent);
        click_chatter("Parent exists : %d",root->find_node(parent));
        root->print();
        sfcb->reverse_print();
        assert(false);
    }
#endif
    if (unlikely(!parent->_leaf_reverse_match(sfcb,p))) {
#if DEBUG_CLASSIFIER_MATCH > 2
        click_chatter("DIF is_default %d Leaf %x %x level %s",parent->default_ptr()->ptr == sfcb,parent->level()->get_data(p).get_long(), sfcb->node_data[0].get_long(),parent->level()->print().c_str());
#endif
        return false;
    } else {
#if DEBUG_CLASSIFIER_MATCH > 2
        click_chatter("MAT is_default %d Leaf %x %x level %s",parent->default_ptr()->ptr == sfcb,parent->level()->get_data(p).get_long(), sfcb->node_data[0].get_long(),parent->level()->print().c_str());
#endif
    }

    while (parent != root) {

#if DEBUG_CLASSIFIER
		if (parent->threads.size() <= click_current_cpu_id() || !parent->threads[click_current_cpu_id()]) {
			click_chatter("[%d] FATAL : Parent should not have this thread",click_current_cpu_id());
			click_chatter("[%d] FATAL : Thread of parent : %s",click_current_cpu_id(),parent->threads.unparse().c_str());
			parent->print();
			assert(parent->threads[click_current_cpu_id()]);
		}
#endif
        FlowNode* child = parent;
        parent = parent->parent();
        flow_assert(parent);
        flow_assert(child != parent);

        //While parent (the old one) is growing, we continue. Because we already matched on that level.
        if (child->level() == parent->level())
            continue;

        if (unlikely(!parent->_node_reverse_match(child,p))) {
#if DEBUG_CLASSIFIER_MATCH > 2
            click_chatter("DIF is_default %d Child %x %x level %s",parent->default_ptr()->ptr == child ,parent->level()->get_data(p).get_long(), child->node_data.get_long(),parent->level()->print().c_str());
#endif
            return false;
        } else {
#if DEBUG_CLASSIFIER_MATCH > 2
            click_chatter("MAT is_default %d Child %x %x level %s",parent->default_ptr()->ptr == child,parent->level()->get_data(p).get_long(), child->node_data.get_long(),parent->level()->print().c_str());
#endif
        }
    };
    return true;
}


FlowControlBlock* FlowClassificationTable::match(Packet* p, FlowNode* parent) {
    FlowNodePtr* child_ptr = 0;
#if DEBUG_CLASSIFIER

    FlowNode* debug_save_root = parent;
#endif
    int level_nr = 0;
    (void)level_nr;
    do {
        bool need_grow = false;
        FlowNodeData data = parent->level()->get_data(p);
        debug_flow("[%d] Data level %d is %016llX %s",click_current_cpu_id(), level_nr++,data.get_long(),parent->level()->print().c_str());
        child_ptr = parent->find(data,need_grow);
        debug_flow("[%d] ->Ptr is %p, is_leaf : %d",click_current_cpu_id(), child_ptr->ptr, child_ptr->is_leaf());
        if (unlikely(IS_FREE_PTR_ANY(child_ptr->ptr) //Do not change ptr here to 0, as we could follow default path without changing this one
#if FLOW_KEEP_STRUCTURE
                || (child_ptr->is_node() && child_ptr->node->released())
#endif
                )) { //Unlikely to create a new node.

            if (parent->get_default().ptr) {
#if HAVE_FLOW_DYNAMIC
                if (parent->level()->is_dynamic()) {
                    if (unlikely(parent->growing())) {
                        //Table is growing, and we could not find a child, we go to the default table
                        /*
#if DEBUG_CLASSIFIER
                        if (parent->getNum() == 0) { //Table is growing, but have no more child.
                            debug_flow("Table %s finished growing, deleting %p, type %s",parent->level()->print().c_str(), parent, parent->name().c_str());
                            flow_assert(parent->getNum() == parent->findGetNum());
                           // assert(false); //The release took care of this //TODO : this actually happens from time to time. I guess when releasing is prevented at the end of the tree. To inquiry. not bad
                        } else {
#if DEBUG_CLASSIFIER
                            click_chatter("Growing table, %d/%d",parent->num, parent->max_size());
#endif
                        }
#endif
*/

                        flow_assert(parent->default_ptr()->node->parent() == parent);
                        parent = parent->default_ptr()->node;
                        continue;
                   } else { //Parent is not growing
                        if (need_grow) {
#if HAVE_STATIC_CLASSIFICATION
                            click_chatter("MAX CAPACITY ACHIEVED, DROPPING");
                            return 0;
#else
                            parent = parent->start_growing(false);
                            if (!parent) {
                                click_chatter("Could not grow !? Dropping flow !");
                                //TODO : release some children
                                return 0;
                            }
                            flow_assert(_root->find_node(parent));
                            continue;
#endif
                        }

#if DEBUG_CLASSIFIER_CHECK
                        if(parent->getNum() != parent->findGetNum()) {
                            click_chatter("%d %d",parent->getNum(), parent->findGetNum());
                            abort();
                        }
                        flow_assert(parent->getNum() < parent->max_size());
#endif
                        parent->inc_num();
                        if (parent->get_default().is_leaf()) { //Leaf are not duplicated, we need to do it ourself
				            debug_flow("[%d] DUPLICATE leaf %p", click_current_cpu_id(),parent->get_default().ptr);
                            //click_chatter("New leaf with data '%x'",data.get_long());
                            //click_chatter("Data %x %x",parent->default_ptr()->leaf->data_32[2],parent->default_ptr()->leaf->data_32[3]);
                            child_ptr->set_leaf(_pool.allocate());
                            flow_assert(child_ptr->leaf);
                            flow_assert(child_ptr->is_leaf());
                            child_ptr->leaf->initialize();
                            child_ptr->leaf->parent = parent;

    /*#if HAVE_FLOW_DYNAMIC
                            child_ptr->leaf->release_fnt = _classifier_release_fnt;
    #endif*/
                            child_ptr->set_data(data);
                            memcpy(&child_ptr->leaf->node_data[1], &parent->default_ptr()->leaf->node_data[1] ,_pool.data_size() - sizeof(FlowNodeData));
    #if DEBUG_CLASSIFIER_MATCH > 3
                            _root->print();
    #endif
                            _root->check(true, false);
#if DEBUG_CLASSIFIER
                            if (parent->threads.weight() == 1)
				                child_ptr->leaf->thread = parent->threads.clz();
                            else
				                child_ptr->leaf->thread = -1;
                            flow_assert(child_ptr->leaf->thread == -1 || child_ptr->leaf->thread == click_current_cpu_id());
#endif
                            flow_assert(reverse_match(child_ptr->leaf, p, debug_save_root));
                            return child_ptr->leaf;
                        } else {
#if FLOW_KEEP_STRUCTURE
                            if (child_ptr->ptr && child_ptr->node->released()) { //Childptr is a released node, just renew it
                                child_ptr->node->renew();
                                child_ptr->set_data(data);
                                //flow_assert(parent->find(data)->ptr == child_ptr->ptr);
                            } else
#endif
                            { //Duplicate node

                                FlowNode* defNode = parent->default_ptr()->node;
                                flow_assert(parent->default_ptr()->node->getNum() == 0);
                                FlowNode* newNode = defNode->level()->create_node(defNode, false, false);
                                newNode->_level = defNode->level();
                                *newNode->default_ptr() = *defNode->default_ptr();
                                child_ptr->set_node(newNode);
                                debug_flow("[%d] DUPLICATE node, new is %p, default is %p",click_current_cpu_id(),child_ptr->node,parent->default_ptr()->ptr);

                                child_ptr->set_data(data);
                                child_ptr->set_parent(parent);
#if DEBUG_CLASSIFIER
                                newNode->threads.clear();
                                newNode->threads.resize(click_max_cpu_ids());
                                newNode->threads[click_current_cpu_id()] = true;
                                flow_assert(parent->threads[click_current_cpu_id()]);
#endif
//                                flow_assert(parent->find(data)->ptr == child_ptr->ptr);
                            }
                        }
                        flow_assert(parent->getNum() == parent->findGetNum());

                        _root->check(true, false);
                    }
                } else
#endif
                { //There is a default but it is not a dynamic level
				flow_assert(child_ptr->node->threads[click_current_cpu_id()]);
                    child_ptr = parent->default_ptr();
                    if (child_ptr->is_leaf()) {
                        _root->check(true, false); //Do nothing if not in debug mode
                        flow_assert(reverse_match(child_ptr->leaf, p, debug_save_root));
                        return child_ptr->leaf;
                    }
                }
            } else {
                click_chatter("ERROR : no classification node and no default path !");// allowed with the "!" symbol
                click_chatter("Level %s, data %lu", parent->level()->print().c_str(),data.get_long());
                _root->print();
                return 0;
            }
        } else if (child_ptr->is_leaf()) {
		    flow_assert(child_ptr->leaf->thread == -1 || child_ptr->leaf->thread == click_current_cpu_id());
#if DEBUG_FLOW
            if (child_ptr->leaf->parent()) {
                flow_assert(reverse_match(child_ptr->leaf, p));
            }
#endif
            return child_ptr->leaf;
        } else { // is an existing node, and not released. Just descend

		flow_assert(child_ptr->node->threads[click_current_cpu_id()]);
        }

        flow_assert(child_ptr->node->parent() == parent);
        parent = child_ptr->node;

        flow_assert(parent);
    } while(1);
}

inline FlowNodeData FlowNodePtr::data() {
    if (is_leaf())
        return leaf->node_data[0];
    else
        return node->node_data;
}

inline FlowNode* FlowNodePtr::parent() const {
    if (is_leaf())
        return (FlowNode*)leaf->parent;
    else
        return node->parent();
}

inline void FlowNodePtr::set_data(FlowNodeData data) {
    if (is_leaf())
        leaf->node_data[0] = data;
    else
        node->node_data = data;
}

inline void FlowNodePtr::set_parent(FlowNode* parent) {
    if (is_leaf())
        leaf->parent = parent;
    else
        node->set_parent(parent);
}

#endif

CLICK_ENDDECLS
#endif
