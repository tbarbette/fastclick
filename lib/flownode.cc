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

FlowNode* FlowNode::start_growing(bool impl) {
                        click_chatter("Table starting to grow (was level %s, node %s)",level()->print().c_str(),name().c_str());
            set_growing(true);
            FlowNode* newNode = level()->create_node(this, true, impl);
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

bool FlowNode::is_dummy() {
    return dynamic_cast<FlowLevelDummy*>(level()) != 0;
}

bool FlowNode::is_full_dummy() {
    return is_dummy() && default_ptr()->is_leaf();
}

/**
 * Ensure that the node has no empty default
 * @return true if the node has all default set
 */
bool FlowNode::has_no_default(bool allow_dynamic) {
    return traverse_all_default_leaf([allow_dynamic](FlowNode* parent) -> bool {
        if (parent->default_ptr()->ptr == 0 && (!allow_dynamic || !parent->level()->is_dynamic())) {
            return false;
        }
        return true;
    });
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
        if (_hint.starts_with("HASH-")) {
            String hint = _hint.substring(_hint.find_left('-') + 1);
            int l = atoi(hint.c_str());
            _level->current_level = l;
            fl = FlowNode::create_hash(l);
        } else if (_hint == "ARRAY") {
            FlowNodeArray* fa = FlowAllocator<FlowNodeArray>::allocate();
            fa->initialize(_level->get_max_value());
            _level->current_level = 100;
            fl = fa;
        } else {
            click_chatter("Unknown hint %s", _hint.c_str());
            abort();
        }
    } else {
        if (_level->get_max_value() == 0)
            fl = new FlowNodeDummy();
        else if (_level->get_max_value() > 256) {
            FlowNode* fh0 = FlowNode::create_hash(0);
            _level->current_level = 0;
    #if DEBUG_CLASSIFIER
            assert(fh0->getNum() == 0);
            fh0->check();
    #endif
            fl = fh0;
        } else {
            FlowNodeArray* fa = FlowAllocator<FlowNodeArray>::allocate();
            _level->current_level = 100;
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
FlowNodePtr*  FlowNodeHash<capacity_n>::find_hash(FlowNodeData data) {
        //click_chatter("Searching for %d in hash table leaf %d",data,leaf);

        flow_assert(getNum() <= max_highwater());
        int i = 0;

        unsigned int idx;

        if (level()->is_long())
            idx = hash64(data.data_32);
        else
            idx = hash32(data.data_32);

        unsigned int insert_idx = UINT_MAX;
        int ri = 0;
#if DEBUG_CLASSIFIER > 1
        click_chatter("Idx is %d, table v = %p, num %d, capacity %d",idx,childs[idx].ptr,getNum(),capacity());
#endif
        if (unlikely(growing())) { //Growing means the table is currently in destructions, so we check for DESTRUCTED_NODE pointers also, and no insert_idx trick
            while (childs[idx].ptr) {
                if (childs[idx].ptr != DESTRUCTED_NODE && childs[idx].data().data_64 == data.data_64)
                    break;
                idx = next_idx(idx);
                i++;
    #if DEBUG_CLASSIFIER > 1
                assert(i <= capacity());
    #endif
            }
        } else {
            while (childs[idx].ptr) {
                if (childs[idx].ptr == DESTRUCTED_NODE || childs[idx].data().data_64 != data.data_64) {
                    if (insert_idx == UINT_MAX &&  childs[idx].is_node() && childs[idx].node->released()) {
                        insert_idx = idx;
                        ri ++;
                    }
                } else {
                    if (insert_idx != UINT_MAX) {
                        debug_flow("Swap IDX %d<->%d",insert_idx,idx);
                        FlowNodePtr tmp = childs[insert_idx];
                        childs[insert_idx] = childs[idx];
                        childs[idx] = tmp;
                        idx = insert_idx;
                    }
                    goto found;
                }
    #if DEBUG_CLASSIFIER > 1
                click_chatter("Collision hash[%d] is taken by %x while searching space for %x !",idx,childs[idx].ptr == DESTRUCTED_NODE ? -1 : childs[idx].data().data_64, data.data_64);
    #endif
                idx = next_idx(idx);
                i++;

    #if DEBUG_CLASSIFIER
                    assert(i <= capacity());
    #endif
            }
            if (insert_idx != UINT_MAX) {
                debug_flow("Recovered IDX %d",insert_idx);
                /*idx = next_idx(idx);
                int j = 0;
                //If we merge a hole, delete the rest
                THIS IS WRONG
                while (j < 16 && childs[idx].ptr && childs[idx].is_node() && childs[idx].node->released()) {
                    childs[idx].node->destroy();
                    childs[idx].node = 0;
                    idx = next_idx(idx);
                    j ++;
                }*/

                idx = insert_idx;
            }
        }
        found:

#if DEBUG_CLASSIFIER > 1
        click_chatter("Final Idx is %d, table v = %p, num %d, capacity %d",idx,childs[idx].ptr,getNum(),capacity());
#endif

        if (i > collision_threshold()) {
            if (!growing()) {
                click_chatter("%d collisions! Hint for a better hash table size (current capacity is %d, size is %d, data is %lu)!",i,capacity(),getNum(),data.data_32);
                click_chatter("%d released in collision !",ri);
                if (childs[idx].ptr == 0 || (childs[idx].is_node() && childs[idx].node->released())) {
                    FlowNode* n = this->start_growing(true);
                    if (n == 0) {
                        click_chatter("ERROR : CANNOT GROW, I'M ALREADY TOO FAT");
                        return &childs[idx];
                    }
                    return n->find(data);
                }
            }
        }

        return &childs[idx];
    }
/*template<int capacity_n>
void FlowNodeHash<capacity_n>::renew() {
    _released = false;

}*/

template<int capacity_n>
void FlowNodeHash<capacity_n>::release_child(FlowNodePtr child, FlowNodeData data) {
    int j = 0;
    unsigned idx;
    if (level()->is_long())
        idx = hash64(data.data_32);
    else
        idx = hash32(data.data_32);
    int i = 0; //Collision number
    while (childs[idx].ptr != child.ptr) {
        idx = next_idx(idx);
        ++i;
#if DEBUG_CLASSIFIER_CHECK
        assert(i < capacity());
#endif
    }

    if (child.is_leaf()) {
        childs[idx].ptr = DESTRUCTED_NODE; //FCB deletion is handled by the caller which goes bottom up
    } else {
        if (unlikely(growing())) { //If we are growing, or the child is growing, we want to destroy the child definitively
            childs[idx].ptr = DESTRUCTED_NODE;
            child.node->destroy();
        } else if (unlikely(child.node->growing())) {
            childs[idx].ptr = 0;
            child.node->destroy();
        } else {
            if (i > hole_threshold() && childs[next_idx(idx)].ptr == 0) { // Keep holes if there are quite a lot of collisions
                click_chatter("Keep hole in %s",level()->print().c_str());
                childs[idx].ptr = 0;
                child.node->destroy();
                i--;
                while (i > (2 * (hole_threshold() / 3))) {
                    idx = prev_idx(idx);
                    if (childs[idx].ptr == DESTRUCTED_NODE) {
                        childs[idx].ptr = 0;
                    } else if (childs[idx].is_node() && childs[idx].node->released()) {
                        childs[idx].node->destroy();
                        childs[idx].ptr = 0;
                    } else
                        break;
                    --i;
                }
            } else {
                child.node->release();
            }
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



template<typename T>
bool FlowLevelGeneric<T>::prune(FlowLevel* other) {
    assert(is_dynamic());
    FlowLevelOffset* ol = dynamic_cast<FlowLevelOffset*>(other);
    if (ol == 0)
        return FlowLevelOffset::prune(other);

    T m = _mask;
    for (int i = 0; i < mask_size(); i++) {
        uint8_t inverted = ol->get_mask(_offset + i);
        //click_chatter("DMask %d %u",i,inverted);
        m = m & (~((T)inverted << ((mask_size() - i - 1)*8)));
    }
    /*
     * Should we care if other is dynamic ?
     * if (other->is_dynamic())
     *  //EG 0/FF is pruned with 0/05/FF
     * else
     * //EG 0/FF is pruned with 0/0/FF
     * -> In each cache we just have to remove the known part from the mask
     */

    if (_mask != m) {
        _mask = m;
        return true;
    }
    return false;
    //click_chatter("DMask is now %x",_mask);
}

FlowNodePtr
FlowLevel::prune(FlowLevel* other, FlowNodeData data, FlowNode* node, bool &changed) {
    if (other->equals(this)) {
        FlowNodePtr* ptr = node->find_or_default(data);
        FlowNodePtr child = *ptr;
        node->dec_num();
        ptr->ptr = 0;
        //TODO delete this;
        changed = true;
        return child;
    }
    return FlowNodePtr(node);
}


template<typename T>
FlowNodePtr FlowLevelGeneric<T>::prune(FlowLevel* other, FlowNodeData data, FlowNode* node, bool &changed) {
        FlowLevelOffset* ol = dynamic_cast<FlowLevelOffset*>(other);
        if (ol == 0)
            return FlowLevel::prune(other,data,node,changed);

        int shift = offset() - ol->offset();
        T shiftedmask = 0;

        for (int i = 0; i < mask_size(); i++) {
            //click_chatter("Mask for %d : %x",i,ol->get_mask(_offset + i));
            shiftedmask = shiftedmask | (ol->get_mask(_offset + i) << ((mask_size() - i - 1)*8));
        }
        //click_chatter("Offset %d, shiftedmask %x mask %x",shift,shiftedmask,_mask);
        if (_mask == shiftedmask) { //Value totally define the child, only keep that one
            //click_chatter("Child");
            FlowNodePtr* ptr = node->find_or_default(data);
            FlowNodePtr child = *ptr;
            node->dec_num();
            ptr->ptr = 0;
            //TODO delete this;
            changed = true;
            return child;
        } else if ((_mask & shiftedmask) != 0) {
            //click_chatter("Overlapping %s %s",this->print().c_str(),other->print().c_str());
            //A B (O 2) other
            //  C D (1 2) --> only keep children with C == B
            T shifteddata;
            if (shift < 0) {
                shifteddata = data.data_64 >> (-shift * 8);
            } else {
                shifteddata = data.data_64 << (shift * 8);
            }
            shifteddata = shifteddata & shiftedmask & _mask;
            //click_chatter("Shifteddata %x",shifteddata);
            node->apply([this,node,shiftedmask,shifteddata](FlowNodePtr* cur){
                    if ((((T)cur->data().data_64) & shiftedmask) != shifteddata) {
                        //click_chatter("%x does not match %x",cur->data().data_64 & shiftedmask, shifteddata);
                        cur->ptr = 0;
                        //TODO delete
                        node->dec_num();
                    }
            });
            _mask = _mask & shiftedmask;
            if (node->getNum() == 0) {
                return *node->default_ptr();
            }
            return FlowNodePtr(node);
        } else {
            //click_chatter("NON Overlapping %s %s",this->print().c_str(),other->print().c_str());
            return FlowNodePtr(node);
        }
}

template<typename T>
FlowLevel* FlowLevelGeneric<T>::optimize() {
    T nmask = _mask;
    int sz = sizeof(T); //Eg. 4 for uint32_t
    int i = 0;
    int offset = _offset;
    while (i < sz && (((nmask >> ((sz - i - 1) * 8)) & 0xff) == (uint8_t)0)) {
        i++;
    }
    int r = 0;

    //00FFFF00 -> i = 1
    //offset+=i;
    sz -= i;
    //FFFF00
    i = 0;
    /*while (i < sz && ((nmask & 0xff) == 0)) { //Imply to rebuild all data
        nmask = nmask >> 8;
        i++;
    }*/
    sz -= i;
    //FFFF

    //if (sz < sizeof(T)) {
        switch(sz) {
            case 0:
                assert(false);
                return 0;
            case 1:
                if (nmask == (uint8_t)-1)
                    return (new FlowLevelField<uint8_t>(offset))->assign(this);
                return (new FlowLevelGeneric<uint8_t>(nmask,offset))->assign(this);
            case 2:
                if (nmask == (uint16_t)-1)
                    return (new FlowLevelField<uint16_t>(offset))->assign(this);
                return (new FlowLevelGeneric<uint16_t>(nmask,offset))->assign(this);
            case 3:
                /*if (offset - 1 & 0x3 == 0)
                    offset -= 1;
                else if (offset & 0x3 == 0)
                    nmask <<= 8;
                else if (offset - 1 & 0x1 == 0)
                    offset -= 1;
                else
                    nmask <<= 8;*/
            case 4:
                if (nmask == (uint32_t)-1)
                    return (new FlowLevelField<uint32_t>(offset))->assign(this);
                return (new FlowLevelGeneric<uint32_t>(nmask,offset))->assign(this);
            case 5:
            case 6:
            case 7:
            case 8:
                if (nmask == (uint64_t)-1)
                    return (new FlowLevelField<uint64_t>(offset))->assign(this);
                return this;//new FlowLevelGeneric<uint64_t>(offset,nmask);
            default:
                assert(false);
        }
    //}

    return this;
}

template class FlowLevelGeneric<uint8_t>;
template class FlowLevelGeneric<uint16_t>;
template class FlowLevelGeneric<uint32_t>;
template class FlowLevelGeneric<uint64_t>;
