// -*- c-basic-offset: 4; related-file-name: "../include/click/flow/flow.hh" -*-
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
#include <stdlib.h>
#include <algorithm>
#include <click/flow/flow.hh>

/********************************
 * FlowNode functions
 *********************************/

FlowNode* FlowNode::start_growing(bool impl) {
            click_chatter("Table starting to grow (was level %s, node %s, num %d, impl %d)",level()->print().c_str(),name().c_str(), num, impl);
            set_growing(true);
            FlowNode* newNode = level()->create_node(this, true, impl);
            flow_assert(newNode->getNum() == 0);
            flow_assert(newNode->findGetNum() == 0);
            if (newNode == 0) {
                //TODO : better to release old flows
                debug_flow("Could not expand");
                return 0;
            }
#if DEBUG_CLASSIFIER
            newNode->threads = threads;
#endif
            newNode->_default = _default;
            newNode->_level = _level;
            newNode->_parent = this;
            _default.set_node(newNode);
            click_chatter("Table is now (level %s, node %s)",newNode->level()->print().c_str(),newNode->name().c_str());

            return newNode;
}


int FlowNode::findGetNum() {
    int count = 0;
    apply([&count](FlowNodePtr* p) {
        flow_assert(p->ptr != DESTRUCTED_NODE);
        flow_assert(p->ptr);
        flow_assert(!IS_FREE_PTR(p->ptr,this));
#if FLOW_KEEP_STRUCTURE
        if (p->is_leaf()
                || !p->node->released()
                )
#endif
            count++;
    });
    return count;
}

FlowNode*
FlowNode::find_node(FlowNode* other) {
    FlowNode* found = 0;
    apply_default([other,&found](FlowNodePtr* p) {
        if (p->ptr && p->is_node()) {
            if (p->node == other || p->node->find_node(other)) {
                found = p->node;
                return false;
            }
        }
        return true;
    });
    return found;
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
void FlowNode::check(bool allow_parent, bool allow_default, bool multithread) {
    FlowNode* node = this;
    NodeIterator it = node->iterator();
    FlowNodePtr* cur = 0;
    /*if (dynamic_cast<FlowNodeDefinition*>(this) != 0)
	    multithread = true;*/
    int num = 0;
    int released = 0;
    int prenum = findGetNum();
    while ((cur = it.next()) != 0) {
        flow_assert(cur->ptr != 0);
        flow_assert(cur->ptr != DESTRUCTED_NODE);
        if (cur->is_node()) {
            if (!allow_parent && cur->node->parent() != node)
                goto error;
#if FLOW_KEEP_STRUCTURE
            if (!cur->node->released())
#else
            if (true)
#endif
            {
		if (multithread && cur->node->threads[click_current_cpu_id()])
			cur->node->check(allow_parent, allow_default, multithread);
                num++;
            } else {
                released++;
            }
        } else {
            cur->leaf->check();
            num++;
        }
    }
    if (num != getNum()) {
        click_chatter("[%d] Number of child error (live count %d with %d released != theorical count %d) in :",click_current_cpu_id(),num, released,getNum());
        print();
        assert(num == prenum);
        assert(prenum == findGetNum()); //Concurrent modification
        assert(num == findGetNum()); //Concurrent modification
        assert(num == getNum());
    }
    if (!allow_default) {
        if (node->default_ptr()->ptr == 0 && dynamic_cast<FlowLevelThread*>(node->level()) == 0) {
            click_chatter("This node has no default path, this is not allowed except for FlowLevelThread: ");
            node->reverse_print();
            assert(false);
        }
    }
    if (node->default_ptr()->ptr != 0) {

        assert(node->get_default().ptr != DESTRUCTED_NODE);
        if (!allow_parent && node->default_ptr()->parent() != node && dynamic_cast<FlowLevelThread*>(node->level()) != 0)
            goto error;
        if (node->default_ptr()->is_node()) {
#if FLOW_KEEP_STRUCTURE
            assert(!node->default_ptr()->node->released());
#endif
            assert(node->level()->is_dynamic() || node->get_default().node->parent() == node);
            if (multithread && node->get_default().node->threads[click_current_cpu_id()])
		        node->get_default().node->check(allow_parent, allow_default,multithread);
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

void FlowNode::reverse_print() {
    FlowNode* p = this;
    String prefix = "";
    while (p != 0) {
        prefix += "--";
        FlowNode::print(p,prefix,-1, true,false);
        p = p->parent();
    };
}

void FlowNode::print(const FlowNode* node,String prefix,int data_offset, bool show_ptr, bool recursive, bool do_release) {
    if (show_ptr) {
        if (node->level()->is_dynamic()) {
#if DEBUG_CLASSIFIER
		click_chatter("%s%s (%s, %d children, dynamic, threads %s) %p Parent:%p",prefix.c_str(),node->level()->print().c_str(),node->name().c_str(), node->getNum() , node->threads.unparse().c_str(),node,node->parent());
#else
            click_chatter("%s%s (%s, %d children, dynamic) %p Parent:%p",prefix.c_str(),node->level()->print().c_str(),node->name().c_str(), node->getNum() ,node,node->parent());
#endif
        } else {
#if DEBUG_CLASSIFIER
            click_chatter("%s%s (%s, %d children, threads %s) %p Parent:%p",prefix.c_str(),node->level()->print().c_str(),node->name().c_str(),node->getNum(), node->threads.unparse().c_str(),node,node->parent());
#else
            click_chatter("%s%s (%s, %d children) %p Parent:%p",prefix.c_str(),node->level()->print().c_str(),node->name().c_str(),node->getNum(),node,node->parent());
#endif
        }
    } else {
        if (node->level()->is_dynamic())
		click_chatter("%s%s (%s, %d children, dynamic)",prefix.c_str(),node->level()->print().c_str(),node->name().c_str(),node->getNum());
        else
            click_chatter("%s%s (%s, %d children)",prefix.c_str(),node->level()->print().c_str(),node->name().c_str(),node->getNum());
    }

    if (!recursive)
        return;
    NodeIterator it = const_cast<FlowNode*>(node)->iterator();
    FlowNodePtr* cur = 0;
    while ((cur = it.next()) != 0) {
        if (!cur->is_leaf()) {
            if (!do_release && cur->node->released())
                continue;
            if (show_ptr)
                click_chatter("%s|-> %lu Parent:%p",prefix.c_str(),cur->data().get_long(),cur->parent());
            else
                click_chatter("%s|-> %lu",prefix.c_str(),cur->data().get_long(), cur->node->released());
            print(cur->node,prefix + "|  ",data_offset, show_ptr, recursive, do_release);
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
            print(node->_default.node,prefix + "|  ",data_offset, show_ptr, recursive, do_release);
        } else {
            node->_default.leaf->print(prefix + "|-> DEFAULT",data_offset, show_ptr);
        }
    }
}


/**
 * Call fnt on all pointer to leaf of the tree
 * If do_final is true, call on normal leaf
 * If do_default is true, call fnt on default path leaf
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
 * Call fnt on all pointer to leaf of the tree and empty defaults
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
bool FlowNode::traverse_all_default_leaf(std::function<bool(FlowNode*)> fnt) {
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
 * Call fnt on all children nodes of the tree, including default ones, but not self. If FNT return false, traversal is stopped for the children
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
FlowNodeDefinition::duplicate(bool recursive,int use_count, bool duplicate_leaf) {
    FlowNodeDefinition* fh = new FlowNodeDefinition(_creator);
    fh->_else_drop = _else_drop;
    fh->_hint = _hint;
    fh->duplicate_internal(this,recursive,use_count, duplicate_leaf);
    return fh;
}

/**
 * Create best structure for this node, and optimize all children
 */
FlowNode*
FlowNodeDefinition::create_final(Bitvector threads) {
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
            fa->initialize(_level->get_max_value() + 1);
            _level->current_level = 100;
            fl = fa;
        } else {
            click_chatter("Unknown hint %s", _hint.c_str());
            abort();
        }
    } else {
        if (_level->get_max_value() == 0)
            fl = new FlowNodeDummy();
        else if (_level->get_max_value() > 255) {
            if (!_level->is_dynamic()) {
                FlowNodeHeap* flh = new FlowNodeHeap();
                flh->initialize(this);
                assert(getNum() == flh->getNum());
                flh->_level = _level;
                flh->_parent = parent();
                flh->apply([flh,threads](FlowNodePtr* cur) {
                    cur->set_parent(flh);
                    if (cur->is_node()) {
                        FlowNodeData data = cur->data();
                        cur->set_node(cur->node->optimize(threads));
                        cur->set_data(data);
                    }
                });
                flh->set_default(_default);
#if DEBUG_CLASSIFIER
                flh->threads = threads;
#endif
                return flh;
            } else {
                FlowNode* fh0 = FlowNode::create_hash(0);
                _level->current_level = 0;
        #if DEBUG_CLASSIFIER
                assert(fh0->getNum() == 0);
                fh0->check();
        #endif
                fl = fh0;
            }
        } else {
            FlowNodeArray* fa = FlowAllocator<FlowNodeArray>::allocate();
            _level->current_level = 100;
            fa->initialize(_level->get_max_value() + 1);
            fl = fa;
        }
    }
    fl->_level = _level;
    fl->_parent = parent();

    NodeIterator it = iterator();
    FlowNodePtr* cur = 0;
    while ((cur = it.next()) != 0) {
        if (cur->data().data_32 > _level->get_max_value()) {
            click_chatter("Data %d is bigger than expected value %d", cur->data().data_32, _level->get_max_value() );
            print();
            assert(false);
        }
        cur->set_parent(fl);
        if (cur->is_node()) {
            fl->add_node(cur->data(),cur->node->optimize(threads));
        } else {
#if DEBUG_CLASSIFIER
			if (threads.weight() == 1)
				cur->leaf->thread = threads.clz();
			else
				cur->leaf->thread = -1;
#endif
            fl->add_leaf(cur->data(),cur->leaf);
        }
    }
    fl->set_default(_default);
#if DEBUG_CLASSIFIER
    fl->threads = threads;
#endif
    return fl;
}

/***************************************
 * FlowNodeHeap
 *************************************/

void
FlowNodeHeap::append_heap(FlowNode* fn,Vector<uint32_t>& vls, int i, int v_left, int v_right) {
    bool need_grow;
    if (v_right < v_left)
        return;
    int middle;
    middle = (v_left + v_right) / 2;
    if (children.size() <= right_idx(i))
        children.resize(right_idx(i) + 1);
    children[i] = *fn->find(FlowNodeData((uint32_t)vls[middle]), need_grow);
    inc_num();

    append_heap(fn, vls,left_idx(i),v_left,middle -1);
    append_heap(fn, vls,right_idx(i),middle + 1, v_right);
}

void
FlowNodeHeap::initialize(FlowNode* fn) {
    //List of encountered values
    Vector<uint32_t> vls = Vector<uint32_t>();
    //Add all seen values to the list
    fn->apply([&vls](FlowNodePtr* ptr){vls.push_back(ptr->data().data_32);});

    //Sort the list, so we take the middle values
    std::sort(vls.begin(), vls.end());
    /*for (int i = 0 ; i < vls.size(); i ++) {
        click_chatter("%d",vls[i]);
    }*/

    int middle = vls.size() / 2;
    bool need_grow;
    children.resize(right_idx(0) + 1);
    children[0] = *fn->find(FlowNodeData((uint32_t)vls[middle]), need_grow);
    inc_num();
    append_heap(fn, vls,left_idx(0),0,middle -1);
    append_heap(fn, vls,right_idx(0),middle + 1, vls.size() - 1);
    for (int i = 0 ; i < children.size(); i ++) {
        debug_flow("[%d] %u ; L:%d R:%d",i,children[i].ptr?children[i].data().data_32:0,left_idx(i),right_idx(i));
        bool need_grow;
        if (children[i].ptr) {
            if (find_heap(children[i].data(),need_grow) != &children[i]) {
                click_chatter("HEAP is wrong, cannot find %d", children[i].data());
                fn->print();
                assert(false);
            }
            assert(right_idx(i) < children.size());
        }
    }
}

/**
 * Delete fully kills children
 */
FlowNodeHeap::~FlowNodeHeap() {
    //Base destructor will delete the default
    for (int i = 0; i < children.size(); i++) {
        if (FLOW_INDEX(children,i).ptr != NULL && FLOW_INDEX(children,i).is_node()) {
               delete FLOW_INDEX(children,i).node;
               FLOW_INDEX(children,i).node = 0;
        }
    }
}
FlowNode* FlowNodeHeap::duplicate(bool recursive,int use_count, bool duplicate_leaf) {
    FlowNodeHeap* fa = FlowAllocator<FlowNodeHeap>::allocate();
    fa->initialize(this);
    fa->duplicate_internal(this,recursive,use_count, duplicate_leaf);
    return fa;
}




/***************************************
 * FlowNodeArray
 *************************************/
/**
 * Destroy puts back the memory and release default
 * @precond at run-time, num is 0 and there is no default
 */
void FlowNodeArray::destroy() {
    flow_assert(pool_allocator_mt_base::dying() || num == 0); //Not true on final destroy

    FlowAllocator<FlowNodeArray>::release(this);
}

/**
 * Delete fully kills children
 */
FlowNodeArray::~FlowNodeArray() {
    //Base destructor will delete the default
    for (int i = 0; i < children.size(); i++) {
        if (FLOW_INDEX(children,i).ptr != NULL && FLOW_INDEX(children,i).is_node()) {
               delete FLOW_INDEX(children,i).node;
               FLOW_INDEX(children,i).node = 0;
        }
    }
}
FlowNode* FlowNodeArray::duplicate(bool recursive, int use_count, bool duplicate_leaf) {
    FlowNodeArray* fa = FlowAllocator<FlowNodeArray>::allocate();
    fa->initialize(children.size());
    fa->duplicate_internal(this, recursive, use_count, duplicate_leaf);
    return fa;
}
void FlowNodeArray::release_child(FlowNodePtr child, FlowNodeData data) {
            if (child.is_leaf()) {
                FLOW_INDEX(children,data.data_32).ptr = 0; //FCB deletion is handled by the caller which goes bottom up
            } else {
                if (growing()) { //We need to destroy the pointer FLOW_KEEP_STRUCTURE as this node will be reused elswhere
			child.node->set_growing(false);
                    child.node->destroy();
                    child.node = 0;
                } else {
#if FLOW_KEEP_STRUCTURE
                    child.node->release();
#else
                    child.node->destroy();
                    child.node = 0;
#endif
                }
            }
            num--;
            flow_assert(getNum() == findGetNum());
};

/******************************
 * FlowNodeHash
 ******************************/

/**
 * Destroy puts back the node in the pool
 * @precond num is 0 and there is no default
 */
template<int capacity_n>
void FlowNodeHash<capacity_n>::destroy() {

    flow_assert(num == 0);
    //This is copy pasted to avoid virtual destruction
#if FLOW_HASH_RELEASE == RELEASE_RESET
    for (int i = 0; i < capacity(); i++) {
        if (!IS_EMPTY_PTR(children[i].ptr, this)) {
            flow_assert(IS_DESTRUCTED_NODE(children[i].ptr,this));
/*            if (children[i].ptr != DESTRUCTED_NODE && children[i].is_node()) {
                children[i].node->destroy();
            }*/
            children[i].ptr = 0;
        }
    }
#elif FLOW_HASH_RELEASE == RELEASE_EPOCH
    this->epoch ++;
    if (this->epoch == MAX_EPOCH) {
        this->epoch = 1;
        for (int i = 0; i < capacity(); i++) {
            flow_assert(IS_DESTRUCTED_NODE(children[i].ptr,this));
            children[i].ptr = 0;
        }
    }
#endif
    FlowAllocator<FlowNodeHash<capacity_n>>::release(this);
}

/**
 * Delete FlowNodehash and its children and default
 * @precond leafs are deleted
 */
template<int capacity_n>
FlowNodeHash<capacity_n>::~FlowNodeHash() {
        for (int i = 0; i < capacity(); i++) {
            if (IS_VALID_NODE(children[i],this)) {
                delete children[i].node;
                children[i].node = 0;
            }
        }
        static_assert(capacity() < INT_MAX / 2, "Capacity must be smaller than INT_MAX/2");
        static_assert(step() < capacity() / 2, "The hash steps must be smaller than the capacity/2 ");
}


template<int capacity_n>
FlowNode*
FlowNodeHash<capacity_n>::duplicate(bool recursive,int use_count, bool duplicate_leaf) {
    FlowNodeHash* fh = FlowAllocator<FlowNodeHash<capacity_n>>::allocate();
#if DEBUG_CLASSIFIER_CHECK
    assert(fh->getNum() == 0);
    fh->check();
#endif
    fh->duplicate_internal(this,recursive,use_count,duplicate_leaf);
    return fh;
}

template<int capacity_n>
FlowNodePtr*  FlowNodeHash<capacity_n>::find_hash(FlowNodeData data, bool &need_grow) {
        flow_assert(getNum() <= max_highwater());
        int i = 0;

        unsigned int idx;
#if HAVE_LONG_CLASSIFICATION
        if (level()->is_long())
            idx = hash64(data.data_32);
        else
#endif
            idx = hash32(data.data_32);

        unsigned int insert_idx = UINT_MAX;
        int ri = 0;
#if DEBUG_CLASSIFIER > 1
        click_chatter("Idx is %d, table v = %p, num %d, capacity %d",idx,children[idx].ptr,getNum(),capacity());
#endif
        if (unlikely(growing())) { //Growing means the table is currently in destructions, so we check for DESTRUCTED_NODE pointers also, and no insert_idx trick
            while (!IS_EMPTY_PTR(children[idx].ptr,this)) {
                if (IS_VALID_PTR(children[idx].ptr,this) && children[idx].data().equals(data))
                    break;
                idx = next_idx(idx);
                i++;
    #if DEBUG_CLASSIFIER > 1
                assert(i <= capacity());
    #endif
            }
        } else {
            while (!IS_EMPTY_PTR(children[idx].ptr,this)) {
                //While there is something in that bucket already
                if (IS_DESTRUCTED_PTR(children[idx].ptr,this)
#if FLOW_KEEP_STRUCTURE
                            || (children[idx].is_node() && children[idx].node->released())
#endif
                        ) {
                    //If destructed node, we can actually use this idx --> set insert_idx to this pointer as we can replace it
                    if (insert_idx == UINT_MAX) {
                        insert_idx = idx;
                        ri ++;
                    }
                } else if (!children[idx].data().equals(data)) {

                } else { //We found the right bucket, that already exists
                    if (insert_idx != UINT_MAX) {
                        //If we have an insert_idx, swap bucket to "compress" elements
                        debug_flow("Swap IDX %d<->%d",insert_idx,idx);
                        FlowNodePtr tmp = children[insert_idx];
                        children[insert_idx] = children[idx];
                        children[idx] = tmp;
                        idx = insert_idx;
                    }
                    goto found;
                }
    #if DEBUG_CLASSIFIER > 1
                click_chatter("Collision hash[%d] is taken by %x while searching space for %x !",idx,children[idx].ptr == DESTRUCTED_NODE ? -1 : children[idx].data().get_long(), data.get_long());
    #endif
                idx = next_idx(idx);
                i++;

                if (i == capacity()) {
                    flow_assert(insert_idx != UINT_MAX);
                    click_chatter("Fully fulled hash table ! GROW NOW !");
                    need_grow = true;
                    break;
                }
            }
            if (insert_idx != UINT_MAX) { //We found an empty pointer if we have an insert_idx, we use that one instead
                debug_flow_2("Recovered IDX %d",insert_idx);
                idx = insert_idx;
            }
        }
        found:

#if DEBUG_CLASSIFIER > 1
        click_chatter("Final Idx is %d, table v = %p, num %d, capacity %d",idx,children[idx].ptr,getNum(),capacity());
#endif

        if (i > collision_threshold() || num >= max_highwater() ) {
            if (!growing()) {
                click_chatter("%d collisions! Hint for a better hash table size at level %s (current capacity is %d, size is %d, data is %lu)!",i,level()->print().c_str(),capacity(),getNum(),data.data_32);
                click_chatter("%d released in collision !",ri);
                //If the found node was free, we can start growing
                need_grow = true;
            }
        }

        return &children[idx];
}

/**
 * Remove the children object that can be found at DATA
 * Will not release leafs child (FCBs)
 * Will destroy node if this node is currently growing
 * If KEEP_STRUCTURe, Will release node if not growing, if not KEEP_STRUCTURE, will destroy child
 *
 */
template<int capacity_n>
void FlowNodeHash<capacity_n>::release_child(FlowNodePtr child, FlowNodeData data) {
    int j = 0;
    unsigned idx;
#if HAVE_LONG_CLASSIFICATION
    if (level()->is_long())
        idx = hash64(data.data_32);
    else
#endif
        idx = hash32(data.data_32);
    int i = 0; //Collision number
    while (children[idx].ptr != child.ptr) {
        idx = next_idx(idx);
        ++i;
        flow_assert(i < capacity());
    }

    if (child.is_leaf()) {
        SET_DESTRUCTED_NODE(children[idx].ptr,this); //FCB deletion is handled by the caller which goes bottom up
    } else {
        if (unlikely(growing())) { //If we are growing, or the child is growing, we want to destroy the child definitively
            SET_DESTRUCTED_NODE(children[idx].ptr,this);
            child.node->set_growing(false);
            child.node->destroy();
        } else { //Child is node and not growing
            //flow_assert(!child.node->growing()); //If the child is growing, the caller has to swap it, not destroy it
            if (i > hole_threshold() && IS_EMPTY_PTR(children[next_idx(idx)].ptr,this)) { // Keep holes if there are quite a lot of collisions
                click_chatter("Keep hole in %s",level()->print().c_str());
                children[idx].ptr = 0;
                child.node->destroy();
                i--;
                while (i > (2 * (hole_threshold() / 3))) {
                    idx = prev_idx(idx);
                    if (IS_FREE_PTR(children[idx].ptr,this)) {
                        children[idx].ptr = 0;
#if FLOW_KEEP_STRUCTURE
                    } else if (children[idx].is_node() && children[idx].node->released()) {
                        children[idx].node->destroy();
                        children[idx].ptr = 0;
#endif
                    } else
                        break;
                    --i;
                }
            } else {
#if FLOW_KEEP_STRUCTURE
                child.node->release();
#else
                SET_DESTRUCTED_NODE(children[idx].ptr,this);
                child.node->destroy();
#endif
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

#define FLOW_DEBUG_PRUNE DEBUG_CLASSIFIER

#if HAVE_DPDK
rte_flow_item* find_layer(Vector<rte_flow_item> &pattern, enum rte_flow_item_type type) {
    for (int i = 0; i < pattern.size(); i++)
        if (pattern[i].type == type)
            return &pattern[i];
    return 0;
}
int FlowLevelOffset::to_dpdk_flow(FlowNodeData data, rte_flow_item_type last_layer, int last_offset, rte_flow_item_type &next_layer, int &next_layer_offset, Vector<rte_flow_item> &pattern, bool is_default) {
        rte_flow_item pat;
                        pat.spec = 0;
                        pat.mask = 0;
                        pat.last = 0;
        if (last_layer == RTE_FLOW_ITEM_TYPE_RAW) {
            if (offset() - last_offset == 12 && mask_size() == 2) {
                click_chatter("Ether type");
                pat.type = RTE_FLOW_ITEM_TYPE_ETH;

                struct rte_flow_item_eth* eth = (struct rte_flow_item_eth*) malloc(sizeof(rte_flow_item_eth));
                struct rte_flow_item_eth* mask = (struct rte_flow_item_eth*) malloc(sizeof(rte_flow_item_eth));
                bzero(eth, sizeof(rte_flow_item_eth));
                bzero(mask, sizeof(rte_flow_item_eth));
                if (is_default) {
                    pat.spec = 0;
                    pat.mask = 0;
                    click_chatter("Default ethertype");
                } else {
                    eth->type = data.data_16;
                    mask->type = -1;
                    click_chatter("Ether Type %d", data.data_16);
                    pat.spec = eth;
                    pat.mask = mask;
                    pat.last = 0;
                    if (eth->type == 0x0008) {
                        next_layer = RTE_FLOW_ITEM_TYPE_IPV4;
                        next_layer_offset = 14;
                    } else if (eth->type == 0x0608) {
                        next_layer = RTE_FLOW_ITEM_TYPE_ARP_ETH_IPV4;
                        next_layer_offset = 14;
                    } else {
                        click_chatter("Unknown ethertype...");
                        next_layer = RTE_FLOW_ITEM_TYPE_END;
                        next_layer_offset = -1;
                    }
                }
                pattern.push_back(pat);
                return 1;
            } else {
                return -1;
            }
        } else if (last_layer == RTE_FLOW_ITEM_TYPE_IPV4) {
            if (offset() - last_offset == 9 && mask_size() == 1) {
                click_chatter("IPV4 type");
                pat.type = RTE_FLOW_ITEM_TYPE_IPV4;

                struct rte_flow_item_ipv4* spec = (struct rte_flow_item_ipv4*) malloc(sizeof(rte_flow_item_ipv4));
                struct rte_flow_item_ipv4* mask = (struct rte_flow_item_ipv4*) malloc(sizeof(rte_flow_item_ipv4));
                bzero(spec, sizeof(rte_flow_item_ipv4));
                bzero(mask, sizeof(rte_flow_item_ipv4));
                if (is_default) {
                    pat.spec = 0;
                    pat.mask = 0;
                    click_chatter("Default ipv4");
                } else {
                    spec->hdr.next_proto_id = data.data_8;
                    mask->hdr.next_proto_id = -1;
                    click_chatter("IPV4 Type %d", data.data_8);
                    pat.spec = spec;
                    pat.mask = mask;
                    pat.last = 0;
                    bool addm = false;
                    if (spec->hdr.next_proto_id == 0x01) {
                        next_layer = RTE_FLOW_ITEM_TYPE_ICMP;
                        next_layer_offset = last_offset + 20;
                        //addm = true; unsupported on mlx
                    } else if (spec->hdr.next_proto_id == 0x06) {
                        next_layer = RTE_FLOW_ITEM_TYPE_TCP;
                        next_layer_offset = last_offset;// + 20;
                        addm = true;
                    } else if (spec->hdr.next_proto_id == 0x11) {
                        next_layer = RTE_FLOW_ITEM_TYPE_UDP;
                        next_layer_offset = last_offset;// + 20;
                        addm = true;
                    } else {
                        click_chatter("Unknown ethertype...");
                        next_layer = RTE_FLOW_ITEM_TYPE_END;
                        next_layer_offset = -1;
                    }
                    if (addm) {
                        rte_flow_item patd;
                        patd.type = next_layer;
                        patd.spec = 0;
                        patd.mask = 0;
                        patd.last = 0;

                        pattern.push_back(pat);
                        pattern.push_back(patd);
                        return 2;
                    }
                }

                pattern.push_back(pat);
                return 1;
            } else
                return -1;
        } else {
            click_chatter("%d offset",               offset() -last_offset);
            rte_flow_item* udp = find_layer(pattern, RTE_FLOW_ITEM_TYPE_UDP);
            rte_flow_item* tcp = find_layer(pattern, RTE_FLOW_ITEM_TYPE_TCP);
            if ((udp || tcp)) {
                click_chatter("Has UDP/TCP layer !");
                if ((offset() - last_offset == 20 || offset() - last_offset == 22 ) && mask_size() == 2) {
                click_chatter("UDP or TCP ports");
                rte_flow_item* pat;
                if (udp) {
                    pat = udp;
                } else {
                    pat = tcp;
                }

                struct rte_flow_item_tcp* spec = (struct rte_flow_item_tcp*) malloc(sizeof(rte_flow_item_tcp));
                struct rte_flow_item_tcp* mask = (struct rte_flow_item_tcp*) malloc(sizeof(rte_flow_item_tcp));
                bzero(spec, sizeof(rte_flow_item_tcp));
                bzero(mask, sizeof(rte_flow_item_tcp));
                if (is_default) {
                    pat->spec = 0;
                    pat->mask = 0;
                    click_chatter("Default TCP or UDP");
                } else {
                    if (offset() - last_offset == 20) {
                        spec->hdr.src_port = data.data_16;
                        mask->hdr.src_port = -1;
                    } else {
                        spec->hdr.dst_port = data.data_16;
                        mask->hdr.dst_port = -1;
                    }
                    click_chatter("Port %d", data.data_16);
                    pat->spec = spec;
                    pat->mask = mask;
                    pat->last = 0;

                    click_chatter("Unimplemented next proto...");
                    next_layer = RTE_FLOW_ITEM_TYPE_END;
                    next_layer_offset = -1;
                }

                return 0;
                }
            }

            rte_flow_item* ipv4 = find_layer(pattern, RTE_FLOW_ITEM_TYPE_IPV4);
            if (ipv4) {
                click_chatter("Has IPV4 layer !");
                if ((offset() - last_offset == 12 || offset() - last_offset  == 16 ) && mask_size() == 4) {
                    click_chatter("IPV4 IP");
                    rte_flow_item_ipv4* spec;
                    rte_flow_item_ipv4* mask;
                    if (!ipv4->spec) {
                        spec = (struct rte_flow_item_ipv4*) malloc(sizeof(rte_flow_item_ipv4));
                        mask = (struct rte_flow_item_ipv4*) malloc(sizeof(rte_flow_item_ipv4));
                        bzero(spec, sizeof(rte_flow_item_ipv4));
                        bzero(mask, sizeof(rte_flow_item_ipv4));
                        ipv4->spec = spec;
                        ipv4->mask = mask;
                    } else {
                        spec = (rte_flow_item_ipv4*)ipv4->spec;
                        mask = (rte_flow_item_ipv4*)ipv4->mask;
                    }
                    if (offset() - last_offset == 12) {
                        spec->hdr.src_addr = data.data_32;
                        mask->hdr.src_addr = -1;
                    } else {
                        spec->hdr.dst_addr = data.data_32;
                        mask->hdr.dst_addr = -1;
                    }
                    next_layer_offset = last_offset;
                    next_layer = last_layer;
                    return 0;
                }
            }
        }

        return -1;
}

#endif

/**
 * Prune a dynamic level with some other
 */
template<typename T>
bool FlowLevelGeneric<T>::prune(FlowLevel* other) {
    assert(is_dynamic());
    FlowLevelOffset* ol = dynamic_cast<FlowLevelOffset*>(other);
    if (ol == 0)
        return FlowLevelOffset::prune(other);

    T m = _mask;
    for (int i = 0; i < mask_size(); i++) {
        uint8_t omask = ol->get_mask(_offset + i);
#if FLOW_DEBUG_PRUNE
        click_chatter("DMask %d (tot %d) mask %x",i,_offset + i,omask);
#endif
        //If the mask overlaps, we remove this part from our mask
        if (omask) {
            uint8_t inverted = omask;
            m = m & (~((T)inverted << ((i)*8)));
        }
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
#if FLOW_DEBUG_PRUNE
        click_chatter("MASK CHANGED ! %x %s",m,print().c_str());
#endif
        return true;
    }
    return false;
    //click_chatter("DMask is now %x",_mask);
}


template<typename T>
String FlowLevelGeneric<T>::print() {
    StringAccum s;
    s << _offset;
    s << "/";
    for (int i = 0; i < sizeof(T); i++) {
        uint8_t t = ((uint8_t*)&_mask)[i];
        s << hex[t >> 4] << hex[t & 0xf];
    }
    return s.take_string();
}

FlowNodePtr
FlowLevel::prune(FlowLevel* other, FlowNodeData data, FlowNode* node, bool &changed) {
    if (other->equals(this)) {
        debug_flow("Pruning identical levels");
        FlowNodePtr* ptr = node->find_or_default(data);
        FlowNodePtr child = *ptr;
        node->dec_num();
        ptr->ptr = 0;
        //TODO delete this;
        changed = true;
        return child;
    } else {
#if FLOW_DEBUG_PRUNE
       click_chatter("Cannot prune %s with %s",node->level()->print().c_str(),other->print().c_str());
#endif
    }
    node->check();
    return FlowNodePtr(node);
}


template<typename T>
FlowNodePtr FlowLevelGeneric<T>::prune(FlowLevel* other, FlowNodeData data, FlowNode* node, bool &changed) {
        FlowLevelOffset* ol = dynamic_cast<FlowLevelOffset*>(other);
        if (ol == 0)
            return FlowLevel::prune(other,data,node,changed);

        int shift = offset() - ol->offset();
        T shiftedmask = 0;
#if FLOW_DEBUG_PRUNE
       click_chatter("Prune %s with %s",node->level()->print().c_str(),other->print().c_str());
#endif
        for (int i = 0; i < mask_size(); i++) {
#if FLOW_DEBUG_PRUNE
            click_chatter("Mask for %d : %x",i,ol->get_mask(_offset + i));
#endif
            shiftedmask = shiftedmask | (ol->get_mask(_offset + i) << ((mask_size() - i - 1)*8));
        }
#if FLOW_DEBUG_PRUNE
        click_chatter("Offset %d, shiftedmask %x mask %x",shift,shiftedmask,_mask);
#endif
        if (_mask == shiftedmask) { //Value totally define the child, only keep that one
#if FLOW_DEBUG_PRUNE
            click_chatter("FlowLevel identical child, only keeping identical child :");
#endif
            FlowNodePtr* ptr = node->find_or_default(data);
            FlowNodePtr child = *ptr;
            node->dec_num();
            ptr->ptr = 0;
            //TODO delete this;
            changed = true;

#if FLOW_DEBUG_PRUNE
            child.print();
#endif
            return child;
        } else if ((_mask & shiftedmask) != 0) {
#if FLOW_DEBUG_PRUNE
            click_chatter("Overlapping %s %s",this->print().c_str(),other->print().c_str());
#endif
            //A B (O 2) other
            //  C D (1 2) --> only keep children with C == B
            T shifteddata;
            if (shift < 0) {
                shifteddata = data.get_long() >> (-shift * 8);
            } else {
                shifteddata = data.get_long() << (shift * 8);
            }
            shifteddata = shifteddata & shiftedmask & _mask;
#if FLOW_DEBUG_PRUNE
            click_chatter("Shifteddata %x",shifteddata);
#endif
            node->apply([this,node,shiftedmask,shifteddata](FlowNodePtr* cur){
                    if ((((T)cur->data().get_long()) & shiftedmask) != shifteddata) {
#if FLOW_DEBUG_PRUNE
                        click_chatter("%x does not match %x",cur->data().get_long() & shiftedmask, shifteddata);
#endif
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
#if FLOW_DEBUG_PRUNE
            click_chatter("NON Overlapping %s %s",this->print().c_str(),other->print().c_str());
#endif
            return FlowNodePtr(node);
        }
}

template<typename T>
FlowLevel* FlowLevelGeneric<T>::optimize(FlowNode* parent) {
    T nmask = _mask;
    int sz = sizeof(T); //Eg. 4 for uint32_t
    int i = 0;//Number of butes at 0
    int offset = _offset;

    while (i < sz && (((nmask >> ((sz - i - 1) * 8)) & 0xff) == (uint8_t)0)) {
        i++;
    }

    int r = 0;



    sz -= i;

    i = 0;
    //Shifting implies to rebuild all data, do it only if there's no child
    //TODO : we could also simply rebuild all data
    if (parent->getNum() == 0) {
        while (i < sz && ((nmask & 0xff) == 0)) {
            nmask = nmask >> 8;
            i++;
            offset++;
        }
        sz -= i;
    }

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
#if HAVE_LONG_CLASSIFICATION
            case 5:
            case 6:
            case 7:
            case 8:
                if (nmask == (uint64_t)-1)
                    return (new FlowLevelField<uint64_t>(offset))->assign(this);
                return this;//new FlowLevelGeneric<uint64_t>(offset,nmask);
#endif
            default:
                assert(false);
        }
    //}

    return this;
}

template class FlowLevelGeneric<uint8_t>;
template class FlowLevelGeneric<uint16_t>;
template class FlowLevelGeneric<uint32_t>;
#if HAVE_LONG_CLASSIFICATION
template class FlowLevelGeneric<uint64_t>;
#endif
