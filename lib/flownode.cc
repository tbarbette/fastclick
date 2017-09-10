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

/********************************
 * FlowNode functions
 *********************************/

FlowNode* FlowNode::start_growing() {
            click_chatter("Table starting to grow (was %s)",level()->print().c_str());
            set_growing(true);
            FlowNode* newNode = level()->create_better_node(this);
            if (newNode == 0) {
                //TODO : better to release old flows
                return 0;
            }
            newNode->_default = _default;
            newNode->_level = _level;
            newNode->_parent = this;
            _default.set_node(newNode);

            return newNode;
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
    if (show_ptr) {
        if (node->level()->is_dynamic())
            click_chatter("%s%s (%s, %d childs, dynamic) %p Parent:%p",prefix.c_str(),node->level()->print().c_str(),node->name().c_str(),node->getNum(),node,node->parent());
        else
            click_chatter("%s%s (%s, %d childs) %p Parent:%p",prefix.c_str(),node->level()->print().c_str(),node->name().c_str(),node->getNum(),node,node->parent());
    } else {
        if (node->level()->is_dynamic())
            click_chatter("%s%s (%s, %d childs, dynamic)",prefix.c_str(),node->level()->print().c_str(),node->name().c_str(),node->getNum());
        else
            click_chatter("%s%s (%s, %d childs)",prefix.c_str(),node->level()->print().c_str(),node->name().c_str(),node->getNum());
    }

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
            flow_assert(node->level()->is_dynamic() || node->_default.node->parent() == node);
            print(node->_default.node,prefix + "|  ",data_offset, show_ptr);
        } else {
            node->_default.leaf->print(prefix + "|-> DEFAULT",data_offset, show_ptr);
        }
    }
}


/**
 * Call fnt on all pointer to leaf of the tree. If do_empty is true, also call on null default ptr.
 */
void  FlowNode::traverse_all_leaves(std::function<void(FlowNodePtr*)> fnt, bool do_final, bool do_default) {
    NodeIterator it = this->iterator();
    FlowNodePtr* cur;
    while ((cur = it.next()) != 0) {
        if (cur->is_leaf()) {
            if (do_final)
                fnt(cur);
        } else {
            cur->node->traverse_all_leaves(fnt, do_final, do_default);
        }
    }

    if (this->default_ptr()->ptr != 0) {
        cur = this->default_ptr();
        if (cur->is_leaf()) {
            if (do_default)
                fnt(cur);
        } else {
            cur->node->traverse_all_leaves(fnt, do_final, do_default);
        }
    }
}

/**
 * Call fnt on all pointer to leaf of the tree. If do_empty is true, also call on null default ptr.
 */
void  FlowNode::traverse_all_leaves_and_empty_default(std::function<void(FlowNodePtr*,FlowNode*)> fnt, bool do_final, bool do_default) {
    NodeIterator it = this->iterator();
    FlowNodePtr* cur;
    while ((cur = it.next()) != 0) {
        if (cur->is_leaf()) {
            if (do_final)
                fnt(cur, this);
        } else {
            cur->node->traverse_all_leaves_and_empty_default(fnt, do_final, do_default);
        }
    }

    if (this->default_ptr()->ptr != 0) {
        if (this->default_ptr()->is_leaf()) {
            if (do_default)
                fnt(this->default_ptr(), this);
        } else {
            this->default_ptr()->node->traverse_all_leaves_and_empty_default(fnt, do_final, do_default);
        }
    } else {
        fnt(this->default_ptr(), this);
    }
}

/**
 * Call fnt on all nodes having an empty default or a leaf default.
 * Semantically, this is traversing all "else", undefined cases
 */
bool  FlowNode::traverse_all_default_leaf(std::function<bool(FlowNode*)> fnt) {
    NodeIterator it = this->iterator();
    FlowNodePtr* cur;
    while ((cur = it.next()) != 0) {
        if (!cur->is_leaf()) {
            if (!cur->node->traverse_all_default_leaf(fnt))
                return false;
        }
    }
    if (this->default_ptr()->ptr == 0) {
        if (!fnt(this))
            return false;
    } else {
        if (this->default_ptr()->is_leaf()) {
            if (!fnt(this))
                return false;
        } else {
            if (!this->default_ptr()->node->traverse_all_default_leaf(fnt))
                return false;
        }
    }
    return true;
}


/**
 * Call fnt on all children nodes of the tree, including default ones, but not self. If FNT return false, traversal is stopped
 */
bool  FlowNode::traverse_all_nodes(std::function<bool(FlowNode*)> fnt) {
    NodeIterator it = this->iterator();
    FlowNodePtr* cur;
    while ((cur = it.next()) != 0) {
        if (!cur->is_leaf()) {
            if (!fnt(cur->node))
                return false;
            if (!cur->node->traverse_all_nodes(fnt))
                return false;
        }
    }
    if (this->default_ptr()->ptr != 0) {
        if (this->default_ptr()->is_node()) {
            if (!fnt(this->default_ptr()->node))
                return false;
            if (!this->default_ptr()->node->traverse_all_nodes(fnt))
                return false;
        }
    }
    return true;
}

/**
 * Call fnt on all parent of the node, including the node itself
 */
void  FlowNode::traverse_parents(std::function<bool(FlowNode*)> fnt) {
    if (!fnt(this))
        return;
    if (parent())
        parent()->traverse_parents(fnt);
    return;
}
/**
 * Call fnt on all parent of the node, including the node itself
 */
void FlowNode::traverse_parents(std::function<void(FlowNode*)> fnt) {
    fnt(this);
    if (parent())
        parent()->traverse_parents(fnt);
    return;
}

FlowNode* FlowNode::create_hash(int l) {
    FlowNode* fl;
    switch(l) {
     case 0:
         fl = FlowAllocator<FlowNodeHash<0>>::allocate();
         break;
     case 1:
         fl = FlowAllocator<FlowNodeHash<1>>::allocate();
         break;
     case 2:
         fl = FlowAllocator<FlowNodeHash<2>>::allocate();
         break;
     case 3:
         fl = FlowAllocator<FlowNodeHash<3>>::allocate();
         break;
     case 4:
         fl = FlowAllocator<FlowNodeHash<4>>::allocate();
         break;
     case 5:
         fl = FlowAllocator<FlowNodeHash<5>>::allocate();
         break;
     case 6:
         fl = FlowAllocator<FlowNodeHash<6>>::allocate();
         break;
     case 7:
         fl = FlowAllocator<FlowNodeHash<7>>::allocate();
         break;
     case 8:
         fl = FlowAllocator<FlowNodeHash<8>>::allocate();
         break;
     default:
         fl = FlowAllocator<FlowNodeHash<9>>::allocate();
         break;
    }
    return fl;
}


/***************************************
 * FlowNodeDefinition
 *************************************/

FlowNodeDefinition*
FlowNodeDefinition::duplicate(bool recursive,int use_count) {
    FlowNodeDefinition* fh = new FlowNodeDefinition();
    fh->_else_drop = _else_drop;
    fh->_hint = _hint;
    fh->duplicate_internal(this,recursive,use_count);
    return fh;
}

/**
 * Create best structure for this node, and optimize all childs
 */
FlowNode*
FlowNodeDefinition::create_final(bool mt_safe) {
    FlowNode * fl;
    //click_chatter("Level max is %u, deletable = %d",level->get_max_value(),level->deletable);
    if (_hint) {
        assert(_hint.starts_with("HASH-"));
        _hint = _hint.substring(_hint.find_left('-') + 1);
        int l = atoi(_hint.c_str());
        fl = FlowNode::create_hash(l);
    } else {
        if (_level->get_max_value() == 0)
            fl = new FlowNodeDummy();
        else if (_level->get_max_value() > 256) {
            FlowNode* fh0 = FlowNode::create_hash(0);
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
    }
    fl->_level = _level;
    //fl->_child_deletable = _level->is_deletable();
    fl->_parent = parent();

    NodeIterator it = iterator();
    FlowNodePtr* cur = 0;
    while ((cur = it.next()) != 0) {
        cur->set_parent(fl);
        if (cur->is_node()) {
            fl->add_node(cur->data(),cur->node->optimize(mt_safe));
        } else {
            fl->add_leaf(cur->data(),cur->leaf);
        }
    }
    fl->set_default(_default);
    return fl;
}



/***************************************
 * FlowNodeArray
 *************************************/
void FlowNodeArray::destroy() {
#if DEBUG_CLASSIFIER_CHECK
    //This will break final destruction
    apply([](FlowNodePtr* p) {
        if (p->ptr) {
            assert(p->is_node());
            assert(p->node->released());
        }
    });
#endif
    FlowAllocator<FlowNodeArray>::release(this);
}
FlowNodeArray::~FlowNodeArray() {
    for (int i = 0; i < childs.size(); i++) {
        if (childs[i].ptr != NULL && childs[i].is_node()) {
               childs[i].node->destroy();
        }
    }
}
FlowNode* FlowNodeArray::duplicate(bool recursive,int use_count) {
    FlowNodeArray* fa = FlowAllocator<FlowNodeArray>::allocate();
    fa->initialize(childs.size());
    fa->duplicate_internal(this,recursive,use_count);
    return fa;
}



/******************************
 * FlowNodeHash
 ******************************/
template<int capacity_n>
void FlowNodeHash<capacity_n>::destroy() {
    flow_assert(num == 0); //Not true on final destroy
    FlowAllocator<FlowNodeHash<capacity_n>>::release(this);
}


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


template<int capacity_n>
void FlowNodeHash<capacity_n>::renew() {
    _released = false;
#if DEBUG_CLASSIFIER_CHECK
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
#endif
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
