// -*- c-basic-offset: 4; related-file-name: "../include/click/flow.hh" -*-
/*
 * flow.{cc,hh} -- the Flow class
 * Tom Barbette
 *
 * Copyright (c) 2017 University of Liege
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software")
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */
#include <click/config.h>
#include <click/glue.hh>
#include <click/flow.hh>
#include <stdlib.h>

/**
 * FlowNode functions
 */
template<int capacity_n>
FlowNode*
FlowNodeHash<capacity_n>::duplicate(bool recursive,int use_count) {
    FlowNodeHash* fh = FlowAllocator<FlowNodeHash<capacity_n>>::allocate();
#if DEBUG_CLASSIFIER_CHECK
    assert(fh->getNum() == 0);
    fh->check();
#endif
    fh->duplicate_internal(this,recursive,use_count);
    return fh;
}


FlowNode* FlowNodeArray::duplicate(bool recursive,int use_count) {
    FlowNodeArray* fa = FlowAllocator<FlowNodeArray>::allocate();
    fa->initialize(childs.size());
    fa->duplicate_internal(this,recursive,use_count);
    return fa;
}
template<int capacity_n>
void FlowNodeHash<capacity_n>::destroy() {
    flow_assert(num == 0);
    /*for (int i = 0; i < capacity(); i++) {
        if (childs[i].ptr && childs[i].ptr != (void*)-1 && childs[i].is_node()) {
            childs[i].node->destroy();
            childs[i].node = 0;
        }
    }*/
    FlowAllocator<FlowNodeHash<capacity_n>>::release(this);
}
void FlowNodeArray::destroy() {
#if DEBUG_CLASSIFIER_CHECK
    apply([](FlowNodePtr* p) {
        if (p->ptr) {
            assert(p->is_node());
            assert(p->node->released());
        }
    });
#endif
    FlowAllocator<FlowNodeArray>::release(this);
}

int FlowNode::findGetNum() {
    int count = 0;
    apply([&count](FlowNodePtr* p) {
        flow_assert(p->ptr != (void*)-1);
        flow_assert(p->ptr);
        if (p->is_leaf() || !p->node->released())
            count++;
    });
    return count;
}


/*FlowNodePtr* FlowNode::get_first_leaf_ptr() {
    FlowNode* parent = this;
    FlowNodePtr* current_ptr = 0;

    do {
        if (parent->getNum() > 0)
            current_ptr = parent->iterator()->next();
        else
            current_ptr = parent->default_ptr();
    } while(!current_ptr->is_leaf() && (parent = current_ptr->node));

    return current_ptr;

}*/

#if DEBUG_CLASSIFIER || DEBUG_CLASSIFIER_CHECK
/**
 * Ensure consistency of the tree
 * @param node
 */
void FlowNode::check(bool allow_parent) {
    FlowNode* node = this;
    NodeIterator it = node->iterator();
    FlowNodePtr* cur = 0;
    int num = 0;
    while ((cur = it.next()) != 0) {
        if (cur->is_node()) {
            if (!allow_parent && cur->node->parent() != node)
                goto error;
            assert(cur->ptr != (void*)-1);
            if (!cur->node->released()) {
                cur->node->check(allow_parent);
                num++;
            }
        } else {
            cur->leaf->check();
            num++;
        }
    }

    if (num != getNum()) {
        click_chatter("Number of child error (live count %d != theorical count %d) in :",num,getNum());
        print();
        assert(num == getNum());
    }

    if (node->default_ptr()->ptr != 0) {

        if (!allow_parent && node->default_ptr()->parent() != node)
            goto error;
        if (node->default_ptr()->is_node()) {
            assert(!node->default_ptr()->node->released());
            assert(node->level()->is_dynamic() || node->get_default().node->parent() == node);
            node->get_default().node->check(allow_parent);
        } else {
            node->default_ptr()->leaf->check();
        }
    }
    return;
    error:
    click_chatter("Parent concistancy error in :");
    print();
    assert(false);
}
#endif

void FlowNode::print(const FlowNode* node,String prefix,int data_offset, bool show_ptr) {
    if (show_ptr)
        click_chatter("%s%s (%s, %d childs) %p Parent:%p",prefix.c_str(),node->level()->print().c_str(),node->name().c_str(),node->getNum(),node,node->parent());
    else
        click_chatter("%s%s (%s, %d childs)",prefix.c_str(),node->level()->print().c_str(),node->name().c_str(),node->getNum());

    NodeIterator it = const_cast<FlowNode*>(node)->iterator();
    FlowNodePtr* cur = 0;
    while ((cur = it.next()) != 0) {
        if (!cur->is_leaf()) {
            if (show_ptr)
                click_chatter("%s|-> %lu Parent:%p",prefix.c_str(),cur->data().data_64,cur->parent());
            else
                click_chatter("%s|-> %lu",prefix.c_str(),cur->data().data_64);
            print(cur->node,prefix + "|  ",data_offset, show_ptr);
        } else {
            cur->leaf->print(prefix + "|->",data_offset, show_ptr);
        }
    }

    if (node->_default.ptr != 0) {
        if (node->_default.is_node()) {
            if (show_ptr)
                click_chatter("%s|-> DEFAULT %p Parent:%p",prefix.c_str(),node->_default.ptr,node->_default.parent());
            else
                click_chatter("%s|-> DEFAULT",prefix.c_str());
            assert(node->level()->is_dynamic() || node->_default.node->parent() == node);
            print(node->_default.node,prefix + "|  ",data_offset, show_ptr);
        } else {
            node->_default.leaf->print(prefix + "|-> DEFAULT",data_offset, show_ptr);
        }
    }
}

//TODO :inline
void FlowNode::release() {
    assert(!growing());
    _released = true;
    apply([](FlowNodePtr* p) {
        if (p->ptr) {
            assert(p->is_node());
            assert(p->node->released());
        }
    });
};

/**
 * Create best structure for this node, and optimize all childs
 */
FlowNode* FlowNode::create_final() {
    FlowNode * fl;
    //click_chatter("Level max is %u, deletable = %d",level->get_max_value(),level->deletable);
    if (_level->get_max_value() == 0)
        fl = new FlowNodeDummy();
    else if (_level->get_max_value() > 256) {
        FlowNodeHash<0>* fh0 = FlowAllocator<FlowNodeHash<0>>::allocate();
#if DEBUG_CLASSIFIER
        assert(fh0->getNum() == 0);
        fh0->check();
#endif
        fl = fh0;
    } else {
        FlowNodeArray* fa = FlowAllocator<FlowNodeArray>::allocate();
        fa->initialize(_level->get_max_value());
        fl = fa;
    }
    fl->_level = _level;
    //fl->_child_deletable = _level->is_deletable();
    fl->_parent = parent();

    NodeIterator it = iterator();
    FlowNodePtr* cur = 0;
    while ((cur = it.next()) != 0) {
        cur->set_parent(fl);
        if (cur->is_node()) {
            fl->add_node(cur->data(),cur->node->optimize());
        } else {
            fl->add_leaf(cur->data(),cur->leaf);
        }
    }
    fl->set_default(_default);
    return fl;
}

template<int capacity_n>
void FlowNodeHash<capacity_n>::renew() {
    _released = false;
    assert(num == 0);
    assert(!growing());
    for (int i = 0; i < capacity(); i++) {
        if (childs[i].ptr) {
            assert(childs[i].ptr != DESTRUCTED_NODE);
            if (childs[i].is_leaf()) {
                assert(childs[i].leaf == NULL);
            } else {
                assert(childs[i].node->released());
            }
        }
    }
}


template<int capacity_n>
void FlowNodeHash<capacity_n>::release_child(FlowNodePtr child, FlowNodeData data) {
    int j = 0;
    unsigned idx;
    if (level()->is_long())
        idx = hash64(data.data_32);
    else
        idx = hash32(data.data_32);
#if DEBUG_CLASSIFIER_CHECK
    int i = 0;
#endif
    while (childs[idx].ptr != child.ptr) {
        idx = next_idx(idx);
#if DEBUG_CLASSIFIER_CHECK
        assert(i++ < capacity());
#endif
    }

    if (child.is_leaf()) {
        childs[idx].ptr = NULL; //FCB deletion is handled by the caller which goes bottom up
    } else {
        if (unlikely(growing())) { //If we are growing, or the child is growing, we want to destroy the child definitively
            childs[idx].ptr = DESTRUCTED_NODE;
            child.node->destroy();
        } else if (unlikely(child.node->growing())) {
            childs[idx].ptr = 0;
            child.node->destroy();
        } else {
            child.node->release();
        }
    }
    num--;
}

template class FlowNodeHash<0>;
template class FlowNodeHash<1>;
template class FlowNodeHash<2>;
template class FlowNodeHash<3>;
template class FlowNodeHash<4>;
template class FlowNodeHash<5>;
template class FlowNodeHash<6>;
template class FlowNodeHash<7>;
template class FlowNodeHash<8>;
template class FlowNodeHash<9>;
