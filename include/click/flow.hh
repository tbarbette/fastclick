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
//#include <openflow/openflow.h>
#include <thread>
#include <assert.h>

CLICK_DECLS

#ifdef HAVE_FLOW



//typedef void (*sfcb_combiner)(FlowControlBlock* old_sfcb, FlowControlBlock* new_sfcb);

#include "flow_nodes.hh"

class FlowClassificationTable : public FlowTableHolder {
public:
    FlowClassificationTable();

    void set_root(FlowNode* node);
    FlowNode* get_root();

    inline FlowControlBlock* match(Packet* p,bool always_dup);
    inline bool reverse_match(FlowControlBlock* sfcb, Packet* p);
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
    //assert(parent->default_ptr()->ptr != sfcb);
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
#if DEBUG_CLASSIFIER
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
        if (child_ptr->ptr == NULL) {
            if (parent->get_default().ptr) {
                if (parent->level()->is_dynamic() || always_dup) {
                    parent->inc_num();
                    if (parent->get_default().is_leaf()) { //Leaf are not duplicated, we need to do it ourself
#if DEBUG_CLASSIFIER_MATCH > 1
                        click_chatter("DUPLICATE leaf");
#endif
                        //click_chatter("New leaf with data '%x'",data.data_64);
                        //click_chatter("Data %x %x",parent->default_ptr()->leaf->data_32[2],parent->default_ptr()->leaf->data_32[3]);
                        child_ptr->set_leaf(_pool.allocate());
                        child_ptr->leaf->initialize();
                        child_ptr->leaf->parent = parent;
#if HAVE_DYNAMIC_FLOW_RELEASE_FNT
                        child_ptr->leaf->release_fnt = _pool_release_fnt;
#endif
                        child_ptr->set_data(data);
                        memcpy(&child_ptr->leaf->node_data[1], &parent->default_ptr()->leaf->node_data[1] ,_pool.data_size() - sizeof(FlowNodeData));
#if DEBUG_CLASSIFIER_MATCH > 3
                        _root->print();
#endif
                        return child_ptr->leaf;
                    } else {
#if DEBUG_CLASSIFIER_MATCH > 1
                        click_chatter("DUPLICATE node");
#endif
                        child_ptr->set_node(parent->default_ptr()->node->duplicate(false, 0));
                        child_ptr->set_data(data);
                        child_ptr->set_parent(parent);
                    }
                } else {
                    child_ptr = parent->default_ptr();
                    if (child_ptr->is_leaf())
                        return child_ptr->leaf;
                }
            } else {
                click_chatter("ERROR : no classification node and no default path !");
                return 0;
            }
        } else if (child_ptr->is_leaf()) {
            return child_ptr->leaf;
        } else { // is an existing node
            if (child_ptr->node->released()) {
                child_ptr->node->renew();
                child_ptr->set_data(data);
            }
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

inline FlowNode* FlowNodePtr::parent() {
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

inline FlowNode* FlowNode::create(FlowNode* parent, FlowLevel* level) {
    FlowNode * fl;
    //click_chatter("Level max is %u, deletable = %d",level->get_max_value(),level->deletable);
    if (level->get_max_value() == 0)
        fl = new FlowNodeDummy();
    else if (level->get_max_value() > 256)
        fl = new FlowNodeHash();
    else
        fl = new FlowNodeArray(level->get_max_value());
    fl->_level = level;
    fl->_child_deletable = level->is_deletable();
    fl->_parent = parent;
    return fl;
}

#endif

CLICK_ENDDECLS
#endif
