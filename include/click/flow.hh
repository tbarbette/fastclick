#ifndef CLICK_FLOW_HH
#define CLICK_FLOW_HH 1

#include <click/timestamp.hh>
#include <click/error.hh>
#include <click/sync.hh>
#include <click/flow_common.hh>
#include <click/packet.hh>
#include <click/multithread.hh>
#include <click/bitvector.hh>
#include <click/vector.hh>
#include <click/list.hh>
#include <click/allocator.hh>

#include <thread>
#include <assert.h>

CLICK_DECLS

#ifdef HAVE_FLOW

#include "flow_nodes.hh"

class FlowClassificationTable : public FlowTableHolder {
public:
    FlowClassificationTable();
    ~FlowClassificationTable();

    void set_root(FlowNode* node);
    FlowNode* get_root();

    inline FlowControlBlock* match(Packet* p,bool always_dup);
    inline bool reverse_match(FlowControlBlock* sfcb, Packet* p);

    typedef struct {
        FlowNode* root;
        int output;
        bool is_default;
    } Rule;
    static Rule parse(String s, bool verbose = false);
    static Rule make_drop_rule(bool ed = false) {
        Rule r = parse("- drop");
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
 * @assert the FCB does not follow any default rule
 */
bool FlowClassificationTable::reverse_match(FlowControlBlock* sfcb, Packet* p) {
    FlowNode* parent = (FlowNode*)sfcb->parent;

    if (unlikely(!parent->_leaf_reverse_match(sfcb,p))) {
#if DEBUG_CLASSIFIER_MATCH > 2
        click_chatter("DIF is_default %d Leaf %x %x level %s",parent->default_ptr()->ptr == sfcb,parent->level()->get_data(p).data_64, sfcb->node_data[0].data_64,parent->level()->print().c_str());
#endif
        return false;
    } else {
#if DEBUG_CLASSIFIER_MATCH > 2
        click_chatter("MAT is_default %d Leaf %x %x level %s",parent->default_ptr()->ptr == sfcb,parent->level()->get_data(p).data_64, sfcb->node_data[0].data_64,parent->level()->print().c_str());
#endif
    }

    do {
        FlowNode* child = parent;
        parent = parent->parent();
        flow_assert(parent);
        if (likely(!parent->_node_reverse_match(child,p))) {
#if DEBUG_CLASSIFIER_MATCH > 2
            click_chatter("DIF is_default %d Child %x %x level %s",parent->default_ptr()->ptr == child ,parent->level()->get_data(p).data_64, child->node_data.data_64,parent->level()->print().c_str());
#endif
            return false;
        } else {
#if DEBUG_CLASSIFIER_MATCH > 2
            click_chatter("MAT is_default %d Child %x %x level %s",parent->default_ptr()->ptr == child,parent->level()->get_data(p).data_64, child->node_data.data_64,parent->level()->print().c_str());
#endif
        }
    } while (parent != _root);
    return true;
}


FlowControlBlock* FlowClassificationTable::match(Packet* p,bool always_dup) {
    always_dup = false;
    FlowNode* parent = _root;
    FlowNodePtr* child_ptr = 0;
#if DEBUG_CLASSIFIER_MATCH > 1
    int level_nr = 0;
#endif
    do {
        FlowNodeData data = parent->level()->get_data(p);
#if DEBUG_CLASSIFIER_MATCH > 1
        click_chatter("[%d] Data level %d is %016llX",click_current_cpu_id(), level_nr++,data.data_64);
#endif
        child_ptr = parent->find(data);
#if DEBUG_CLASSIFIER_MATCH > 1
        click_chatter("->Ptr is %p, is_leaf : %d",child_ptr->ptr, child_ptr->is_leaf());
#endif

        if (unlikely(child_ptr->ptr == NULL || (child_ptr->ptr == (void*)-1) //Do not change ptr here to 0, as we could follow defautl path without changing this one
#if FLOW_KEEP_STRUCTURE
                || (child_ptr->is_node() && child_ptr->node->released())
#endif
                )) { //Unlikely to create a new node.

            if (parent->get_default().ptr) {
                if (parent->level()->is_dynamic() || always_dup) {
                    if (unlikely(parent->growing())) {
                        //Table is growing, look at the child for new element
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
                        parent = parent->default_ptr()->node;
                        continue;
                    } else if (unlikely(parent->num >= parent->max_size())) { //Parent is not growing, but we should start growing
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
                        continue;

#endif
                    } else { //Parent is not growing, and we don't need to start growing (likely)
#if DEBUG_CLASSIFIER_CHECK
                        if(parent->getNum() != parent->findGetNum()) {
                            click_chatter("%d %d",parent->getNum(), parent->findGetNum());
                            abort();
                        }
                        assert(parent->getNum() < parent->max_size());
#endif
                        parent->inc_num();
                        if (parent->get_default().is_leaf()) { //Leaf are not duplicated, we need to do it ourself
    #if DEBUG_CLASSIFIER_MATCH > 1
                            click_chatter("DUPLICATE leaf");
    #endif
                            //click_chatter("New leaf with data '%x'",data.data_64);
                            //click_chatter("Data %x %x",parent->default_ptr()->leaf->data_32[2],parent->default_ptr()->leaf->data_32[3]);
                            child_ptr->set_leaf(_pool.allocate());
                            flow_assert(child_ptr->leaf);
                            flow_assert(child_ptr->is_leaf());
                            child_ptr->leaf->initialize();
                            child_ptr->leaf->parent = parent;
    /*#if HAVE_DYNAMIC_FLOW_RELEASE_FNT
                            child_ptr->leaf->release_fnt = _classifier_release_fnt;
    #endif*/
                            child_ptr->set_data(data);
                            memcpy(&child_ptr->leaf->node_data[1], &parent->default_ptr()->leaf->node_data[1] ,_pool.data_size() - sizeof(FlowNodeData));
    #if DEBUG_CLASSIFIER_MATCH > 3
                            _root->print();
    #endif
                            _root->check(true);
                            return child_ptr->leaf;
                        } else {
#if FLOW_KEEP_STRUCTURE
                            if (child_ptr->ptr && child_ptr->node->released()) { //Childptr is a released node, just renew it
                                child_ptr->node->renew();
                                child_ptr->set_data(data);
                                flow_assert(parent->find(data)->ptr == child_ptr->ptr);
                            } else
#endif
                            {
                                flow_assert(parent->default_ptr()->node->getNum() == 0);
                                FlowNode* newNode = parent->level()->create_node(parent->default_ptr()->node, false, false);
                                newNode->_level = parent->default_ptr()->node->level();
                                *newNode->default_ptr() = *parent->default_ptr()->node->default_ptr();
                                child_ptr->set_node(newNode);
        #if DEBUG_CLASSIFIER_MATCH > 1
                                click_chatter("DUPLICATE node, new is %p",child_ptr->node);
        #endif

                                child_ptr->set_data(data);
                                child_ptr->set_parent(parent);
                                flow_assert(parent->find(data)->ptr == child_ptr->ptr);
                            }
                        }
                        flow_assert(parent->getNum() == parent->findGetNum());
                    }
                } else { //There is a default but it is not a dynamic level, nor always_dup is set
                    child_ptr = parent->default_ptr();
                    if (child_ptr->is_leaf()) {
                        _root->check(true);
                        return child_ptr->leaf;
                    }
                }
            } else {
                click_chatter("ERROR : no classification node and no default path !");// allowed with the "!" symbol
                _root->print();
                return 0;
            }
        } else if (child_ptr->is_leaf()) {
            return child_ptr->leaf;
        } else { // is an existing node, and not released. Just descend

        }
        parent = child_ptr->node;

        assert(parent);
    } while(1);



    /*int action_id = 0;
	  //=OXM_OF_METADATA_W
	struct ofp_action_header* action = action_table[action_id];
	switch (action->type) {
		case OFPAT_SET_FIELD:
			struct ofp_action_set_field* action_set_field = (struct ofp_action_set_field*)action;
			struct ofp_match* match = (struct ofp_match*)action_set_field->field;
			switch(match->type) {
				case OFPMT_OXM:
					switch (OXM_HEADER(match->oxm_fields)) {
						case OFPXMC_PACKET_REGS:

					}
					break;
				default:
					click_chatter("Error : action field type %d unhandled !",match->type);
			}
			break;
		default:
			click_chatter("Error : action type %d unhandled !",action->type);
	}*/
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
