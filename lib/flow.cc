// -*- c-basic-offset: 4; related-file-name: "../include/click/flow.hh" -*-
/*
 * flow.{cc,hh} -- the Flow class
 * Tom Barbette
 *
 * Copyright (c) 2015 University of Liege
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
#include <regex>

CLICK_DECLS

#ifdef HAVE_FLOW


__thread FlowControlBlock* fcb_stack = 0;
__thread FlowTableHolder* fcb_table = 0;


/*******************************
 * FlowClassificationTable
 *******************************/
FlowClassificationTable::FlowClassificationTable() : _root(0)
{

}
void FlowClassificationTable::set_root(FlowNode* node) {
    assert(node);
    assert(_pool_release_fnt);
    _root = node;
/*#if HAVE_DYNAMIC_FLOW_RELEASE_FNT
    auto fnt = [this](FlowControlBlock* fcb){
        fcb->release_fnt = _pool_release_fnt;
    };
    node->traverse<decltype(fnt)>(fnt);
#endif*/
}

FlowNode* FlowClassificationTable::get_root() {
    return _root;
}

void FlowTableHolder::set_release_fnt(SubFlowRealeaseFnt pool_release_fnt) {
	_pool_release_fnt = pool_release_fnt;
}

FlowClassificationTable::Rule FlowClassificationTable::parse(String s, bool verbose) {
    bool subflow = false;
    bool thread = false;

    std::regex reg("((agg )?((?:(?:ip)?[+-]?[0-9]+/[0-9a-fA-F]+?/?[0-9a-fA-F]+?(?:[:]HASH-[0-9]+)?[!]? ?)+)|-)( keep)?( [0-9]+| drop)?",
             std::regex_constants::icase);
    std::regex classreg("(ip)?[+]?([-]?[0-9]+)/([0-9a-fA-F]+)?/?([0-9a-fA-F]+)?([:]HASH-[0-9]+)?([!])?",
                 std::regex_constants::icase);

    FlowNode* root = 0;

    FlowNodePtr* parent_ptr = 0;

    bool deletable_value = false;
    int output = 0;
    bool is_default = false;

    std::smatch result;
    std::string stdstr = std::string(s.c_str());

    if (s == "thread") {
        thread = true;
        /*      } else if (conf[i] == "subflow") {
                            subflow = true;*/
    } else if (std::regex_match(stdstr, result, reg)){
        FlowNodeData lastvalue = (FlowNodeData){.data_64 = 0};

        std::string other = result.str(1);
        std::string aggregate = result.str(2);
        std::string keep = result.str(4);
        std::string deletable = result.str(5);

        if (keep == " keep")
            deletable_value = true;
        else if (deletable == " drop")
            output = -1;
        else if (deletable != "") {
            output = std::stoi(deletable);
        } else {
            output = INT_MAX;
        }


        FlowNode* parent = 0;
        if (other != "-") {
            std::string classs = result.str(3);

            std::regex_iterator<std::string::iterator> it (classs.begin(), classs.end(), classreg);
            std::regex_iterator<std::string::iterator> end;

            while (it != end)
            {
                if (verbose)
                    click_chatter("Class : %s",it->str(0).c_str());
                std::string layer = it->str(1);
                std::string offset = it->str(2);
                std::string value = it->str(3);
                std::string mask = it->str(4);
                std::string important = it->str(6);
                std::string hint = it->str(5);

                if (verbose)
                    click_chatter("o : %s, v : %s, m : %s",offset.c_str(),value.c_str(), mask.c_str());

                unsigned long valuev = 0xffffffff;
                unsigned long maskv = 0xffffffff;

                FlowLevel* f;

                if (value != "" && value != "-") {
                    valuev = std::stoul(value,nullptr,16);
                    if (value.length() <= 2) {

                    } else if (value.length() <= 4) {
                        valuev = htons(valuev);
                    } else if (value.length() <= 8) {
                        valuev = htonl(valuev);
                    } else {
                        valuev = __bswap_64(valuev);
                    }
                }
                if (verbose)
                    click_chatter("Mask is '%s'",mask.c_str());
                if (mask != "")
                    maskv = std::stoul(mask,nullptr,16);
                else
                    maskv = (1 << value.length() * 4) - 1;

                //TODO error for > 64

                if (aggregate == "agg ") {
                    FlowLevelAggregate* fl = new FlowLevelAggregate();
                    if (offset == "") {
                        fl->offset = 0;
                    } else {
                        fl->offset = std::stoul(offset);
                    }
                    fl->mask = maskv;
                    if (verbose)
                        click_chatter("AGG Offset : %d, mask : 0x%lx",fl->offset,fl->mask);
                    f = fl;
                } else if (maskv <= UINT8_MAX){
                    FlowLevelGeneric8* fl = new FlowLevelGeneric8();
                    fl->set_match(std::stoul(offset),maskv);
                    if (verbose)
                        click_chatter("HASH8 Offset : %d, mask : 0x%lx",fl->offset(),fl->mask());
                    f = fl;
                } else if (maskv <= UINT16_MAX){
                    FlowLevelGeneric16* fl = new FlowLevelGeneric16();
                    fl->set_match(std::stoul(offset),maskv);
                    if (verbose)
                        click_chatter("HASH16 Offset : %d, mask : 0x%lx",fl->offset(),fl->mask());
                    f = fl;
                } else if (maskv <= UINT32_MAX){
                    FlowLevelGeneric32* fl = new FlowLevelGeneric32();
                    fl->set_match(std::stoul(offset),maskv);
                    if (verbose)
                        click_chatter("HASH32 Offset : %d, mask : 0x%lx",fl->offset(),fl->mask());
                    f = fl;
                } else {
                    FlowLevelGeneric64* fl = new FlowLevelGeneric64();
                    fl->set_match(std::stoul(offset),maskv);
                    if (verbose)
                        click_chatter("HASH64 Offset : %d, mask : 0x%lx",fl->offset(),fl->mask());
                    f = fl;
                }

                FlowNodeDefinition* node = new FlowNodeDefinition();

                if (hint != "") {
                    node->_hint = String(hint.substr(1).c_str());

                }

                node->_level = f;
                //node->_child_deletable = f->is_deletable();
                node->_parent = parent;
                if (important == "!") {
                    //click_chatter("Important !");
                    node->_else_drop = true; //TODO : this is not really used anymore, all rules are "else drop" as all FlowDispatcher will add an else drop
                }

                if (root == 0) {
                    root = node;
                } else {
                    parent_ptr->set_node(node);
                    parent_ptr->set_data(lastvalue);
                    if (parent_ptr != parent->default_ptr())
                        parent->inc_num();
                }

                parent = node;

                if (maskv & valuev == 0) { //If a mask is provided, value is dynamic
                    //click_chatter("Dynamic node to output %d",output);
                    parent_ptr = node->default_ptr();
                    node->level()->set_dynamic();
                    lastvalue = (FlowNodeData){.data_64 = (uint64_t)-1};
                } else {
                    //click_chatter("Value %d to output %d",valuev, output);
                    lastvalue = (FlowNodeData){.data_64 = valuev};
                    parent_ptr = node->find(lastvalue);
                }

                ++it;
            }
            if (parent_ptr != parent->default_ptr())
                parent->inc_num();
        } else {
            if (verbose)
                click_chatter("Class : -");
            FlowLevel*  f = new FlowLevelDummy();
            FlowNodeDefinition* fl = new FlowNodeDefinition();
            fl->_level = f;
            //fl->_child_deletable = f->is_deletable();
            fl->_parent = root;
            root = fl;
            parent = root;
            //click_chatter("Default node to output %d",output);
            parent_ptr = root->default_ptr();
            is_default = true;
        }

        FlowControlBlock* fcb = FCBPool::biggest_pool->allocate_empty();
        parent_ptr->set_leaf(fcb);
        parent_ptr->set_data(lastvalue);
        parent_ptr->leaf->parent = parent;
        parent_ptr->leaf->acquire(1);


        root->check();
    }

#if DEBUG_CLASSIFIER
    click_chatter("Parse result of %s : ",s.c_str());
    root->print();
#endif
    if (!s.empty())
        assert(root);
    return FlowClassificationTable::Rule{.root = root, .output = output, .is_default = is_default};
}

#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT


/**
 * @precond Timeout is not in list already
 */
void FlowTableHolder::release_later(FlowControlBlock* fcb) {
    fcb_list& head = old_flows.get();;
#if DEBUG_CLASSIFIER_TIMEOUT_CHECK > 1
    assert(!(fcb->flags & FLOW_TIMEOUT_INLIST));
    assert(!head.find(fcb));
    assert(head.count() == head.find_count());
#endif
    fcb->next = head._next;
    head._next = fcb;
    ++head._count;
    fcb->flags |= FLOW_TIMEOUT_INLIST;
#if DEBUG_CLASSIFIER_TIMEOUT_CHECK > 1
    assert(head.count() == head.find_count());
#endif

}

bool FlowTableHolder::check_release() {
    fcb_list& head = old_flows.get();
    FlowControlBlock* next = 0;
    FlowControlBlock* b = head._next;
    FlowControlBlock** prev = &head._next;
    Timestamp now = Timestamp::recent_steady();

    bool released_something = false;


#if DEBUG_CLASSIFIER_TIMEOUT_CHECK || DEBUG_CLASSIFIER_TIMEOUT
    assert(head.count() == head.find_count());
#endif
    while (b != 0) {
        next = b->next;
        if (b->count() > 0) {
#if DEBUG_CLASSIFIER_TIMEOUT > 2
            click_chatter("FCB %p not releasable anymore as UC is %d",b,b->count());
#endif
            released_something = true;
            b->flags &= ~FLOW_TIMEOUT_INLIST;
            *prev = b->next;
            head._count--;
        }  else if (b->timeoutPassed(now)) {
#if DEBUG_CLASSIFIER_TIMEOUT > 2
            click_chatter("FCB %p has passed timeout",b);
#endif
            released_something = true;
            b->flags = 0;
            *prev = b->next;
            head._count--;
            b->_do_release();
        } else {
            unsigned t = (b->flags >> FLOW_TIMEOUT_SHIFT);
#if DEBUG_CLASSIFIER_TIMEOUT > 1
            click_chatter("Time passed : %d/%d",(now - b->lastseen).msecval(),t);
#endif
            prev = &b->next;
        }
        b = next;
    }

#if DEBUG_CLASSIFIER_TIMEOUT > 0
    click_chatter("Released  %d",head.count());
#endif
#if DEBUG_CLASSIFIER_TIMEOUT_CHECK > 0
    assert(head.find_count() == head.count());
#endif
    return released_something;
}
#endif

void FlowNodePtr::node_combine_ptr(FlowNode* parent, FlowNodePtr other, bool as_child) {
    if (other.is_leaf()) {
        assert(!as_child);

        auto fnt = [other](FlowNode* parent) -> bool {
            if (parent->default_ptr()->ptr == 0) {
                parent->default_ptr()->set_leaf(other.leaf->duplicate(1));
                parent->default_ptr()->set_parent(parent);
            } else {
                parent->default_ptr()->leaf->combine_data(other.leaf->data);
            }
            return true;
        };
        this->node->traverse_all_default_leaf(fnt);
    } else {
        FlowNode* new_node = node->combine(other.node, as_child);
        if (new_node != node) {
            node = new_node;
            node->set_parent(parent);
        }
    }
}

/*******************************
 * FlowNode
 *******************************/

/**
 * Combine this rule with rule of lower priority if its level is not equal
 */
FlowNode* FlowNode::combine(FlowNode* other, bool as_child, bool priority) {
    //TODO : priority is not used as of now, it is always assumed true. We could relax some things.
	if (other == 0) return this;
    other->check();
    this->check();

	assert(other->parent() == 0);

	if (dynamic_cast<FlowLevelDummy*>(this->level()) != 0) {
	        debug_flow("COMBINE : I am dummy")
	        if (_default.is_leaf()) {
	            other->leaf_combine_data(_default.leaf, as_child, !as_child);
	            //TODO delete this;
	            return other;
	        } else {
	            FlowNode* node = _default.node;
	            _default.ptr = 0;
	            //TODO delete this;
	            node->set_parent(0);
	            return node->combine(other, as_child, priority);
	        }
	}

	if (dynamic_cast<FlowLevelDummy*>(other->level()) != 0) { //Other is dummy :
	    debug_flow("COMBINE : Other is dummy")
	    //If other is a dummy (and we're not)
	    if (other->_default.is_leaf()) {
	        debug_flow("COMBINE : Other is a leaf (as child %d):",as_child)
	        //other->_default.leaf->print("");
	        if (as_child) {
	            this->leaf_combine_data(other->_default.leaf, true, true);

	        } else {
	            /*
	             * Duplicate the leaf for all default routes
	             */
                auto fnt = [other](FlowNode* parent) -> bool {
                    if (parent->default_ptr()->ptr == 0) {
                        parent->default_ptr()->set_leaf(other->_default.leaf->duplicate(1));
                        parent->default_ptr()->set_parent(parent);
                    }
                    return true;
                };
                this->traverse_all_default_leaf(fnt);
	            //this->default_ptr()->default_combine(this, other->default_ptr(), as_child);
	            other->default_ptr()->ptr = 0;
	        }

	        //TODO delete other;
	        return this;
	    } else {
	        FlowNode* node = other->_default.node;
	        other->_default.node = 0;
	        node->set_parent(0);
	        //TODO delete other;
	        return this->combine(node,as_child,priority);
	    }
	}

	if (as_child)
	    __combine_child(other);
	else
	    __combine_else(other);
	return this;
}

void FlowNode::__combine_child(FlowNode* other) {
	if (level()->equals(other->level())) { //Same level
#if DEBUG_CLASSIFIER
		click_chatter("COMBINE : same level");
#endif

		FlowNode::NodeIterator other_childs = other->iterator();

        /**
         * We add each of the other child to us.
         * - If the child does not exist, we add ourself simply as a child
         * - If the child exist, we combine the item with the child
         */
		FlowNodePtr* other_child_ptr;
		while ((other_child_ptr = other_childs.next()) != 0) { //For each child of the other node
#if DEBUG_CLASSIFIER
			click_chatter("COMBINE : taking child %lu",other_child_ptr->data().data_64);
#endif

			FlowNodePtr* child_ptr = find(other_child_ptr->data());
			if (child_ptr->ptr == 0) { //We have no same data, so we just append the other's child to us
				*child_ptr = *other_child_ptr;
				child_ptr->set_parent(this);
				inc_num();
			} else { //There is some data in our child that is the same as the other child
				if (child_ptr->is_leaf() || other_child_ptr->is_leaf()) {
#if DEBUG_CLASSIFIER
					click_chatter("Combining leaf??? This error usually happens when rules overlap.");
					child_ptr->print();
					other_child_ptr->print();
					assert(false);

#endif
				} else { //So we combine our child node with the other child node
				    //We must set the parent to null, so the combiner nows he can play with that node
				    other_child_ptr->node->set_parent(0);
					child_ptr->node = child_ptr->node->combine(other_child_ptr->node,true);
					child_ptr->node->set_parent(this);

				}
			}

			//Delete the child of the combined rule
			other_child_ptr->node = 0;
			other->dec_num();
		}
        assert(other->getNum() == 0);

        //Unsupported as of now
/*        if (this->default_ptr()->ptr != 0 || other->default_ptr()->ptr == 0) {
            click_chatter("Unsupported operation, combine as_child :");
            this->print();
            other->print();
        }*/

        //If other had a default, we need to merge it
        if (other->default_ptr()->ptr != 0) { //Other had default
            other->default_ptr()->set_parent(0);
            this->default_ptr()->default_combine(this, other->default_ptr(), true);
            other->default_ptr()->ptr = 0;
        } //ELse nothing to do as we can keep our default as it, if any*/
		//TODO delete other;
        this->check();
		return;
	}
    /*
    else if (dynamic_cast<FlowLevelOffset*>(level()) && dynamic_cast<FlowLevelOffset*>(other->level())) {
#if DEBUG_CLASSIFIER
		click_chatter("COMBINE : two level offset");
#endif
        assert(false);
		FlowLevelOffset* o1 = dynamic_cast<FlowLevelOffset*>(level());
		FlowLevelOffset* o2 = dynamic_cast<FlowLevelOffset*>(other->level());

//if (o1->get_max_value() == o2->get_max_value() && abs(o1->offset() - o2->offset())) { //Offset is different

//		}
	}
*/

	 {

	    debug_flow("Combining different tables (%s -> %s) (as_child is %d) !",
                level()->print().c_str(),
                other->level()->print().c_str(),
                true);
            debug_flow("Adding other as child of all children leaf");
            //In other terms, if the packet does not match any of the rules, it will go through "other"
#if DEBUG_CLASSIFIER
            this->print();
            other->print();
#endif
            this->replace_leaves(other, true, false);
            //Well, it's not that complicated finally.
            this->check();
            return;
	}
	//It is too dangerous to keep a default exit case, each path and possible combination must return right by itself
	assert(false);
}

/**
 * Add a rule to all default path
 */
void FlowNode::__combine_else(FlowNode* other) {

    /**
     * Function replacing all default leaf and empty default per other
     */
    /*auto fnt = [this,other](FlowNode* parent) -> bool {
            if (parent->_default.ptr == 0) {
                click_chatter("Creating default leaf");

                //Set default takes care of setting child's parent
                FlowNodePtr no(other->duplicate(true, 1));

                //Prune the downward tree with all values of the future new parent
                FlowNode* gparent = parent;
                FlowNodeData gdata = {0};
                bool was_default = true;
                while (gparent != NULL) {
                    if (!was_default)
                        no = no.node->prune(gparent->level(),gdata);
                    if (!no.ptr) { //Completely pruned, keep the _default as it.
                        return true;
                    }
                    if (no.is_leaf()) {
                        break;
                    }
                    gdata = gparent->node_data;
                    FlowNode* child = gparent;
                    gparent = gparent->parent();
                    was_default = !gparent || child == gparent->default_ptr()->node;
                }
                if (no.is_leaf()) {
                    *parent->default_ptr() = no;
                } else {
                    assert (parent == parent->combine(no.node, false));
                }
            } else {
                click_chatter("Replacing default leaf");
                assert(parent->default_ptr()->is_leaf());
                parent->_default.replace_leaf_with_node(other);
            }
            return true;
        };

*/

    if (level()->equals(other->level())) { //Same level
        debug_flow("COMBINE-ELSE : same level");
        FlowNode::NodeIterator other_childs = other->iterator();

        /**
         * We add each of the other child to us.
         * - If the child does not exist, we add ourself simply as a child
         * - If the child exist, we combine the item with the child
         */
        FlowNodePtr* other_child_ptr;
        while ((other_child_ptr = other_childs.next()) != 0) { //For each child of the other node
            debug_flow("COMBINE : taking child %lu",other_child_ptr->data().data_64);

            FlowNodePtr* child_ptr = find(other_child_ptr->data());
            if (child_ptr->ptr == 0) { //We have no same data, so we just append the other's child to us
                *child_ptr = *other_child_ptr;
                child_ptr->set_parent(this);
                inc_num();
            } else { //There is some data in our child that is the same as the other child
                if (child_ptr->is_leaf() && other_child_ptr->is_leaf()) {
                    //DO nothing, this is a else rule, mine take precedence
                } else if (child_ptr->is_node() && other_child_ptr->is_node()) {
                    //So we combine our child node with the other child node
                    //We must set the parent to null, so the combiner nows he can play with that node
                    other_child_ptr->node->set_parent(0);
                    child_ptr->node = child_ptr->node->combine(other_child_ptr->node,false);
                    child_ptr->node->set_parent(this);
                } else {
                    assert(false);
                }
            }

            //Delete the child of the combined rule
            other_child_ptr->node = 0;
            other->dec_num();
        }

        assert(other->getNum() == 0);
        //If other had a default, we need to merge it
        if (other->default_ptr()->ptr != 0) { //Other had default
            this->default_ptr()->default_combine(this, other->default_ptr(), true);
            other->default_ptr()->ptr = 0;
        } //ELse nothing to do as we can keep our default as it, if any
        //TODO : delete other;
        this->check();
        return;
    }

    debug_flow("Mhh... No easy combine. Combining other to all children and default");
    //In other terms, if the packet does not match any of the rules, it will go through "other"
    this->debug_print();
    other->debug_print();

    NodeIterator it = iterator();
    FlowNodePtr* cur;
    FlowNodePtr Vpruned_default(other->duplicate(true, 1));
    while ((cur = it.next()) != 0) {
        if (!cur->is_leaf()) {
            cur->node_combine_ptr(this, other->duplicate(true, 1)->prune(level(), cur->data()),false);
        } else {
            if (Vpruned_default.is_node()) { //Other is guaranteed to be not null here
                Vpruned_default = Vpruned_default.node->prune(level(), cur->data(), true);
            }
        }
    }
    this->default_ptr()->default_combine(this, &Vpruned_default, false);
    //TODO : delete other
    this->check();
    return;
}

/**
 * Correct iif this FLowNodePtr is a default ptr
 */
void FlowNodePtr::default_combine(FlowNode* p, FlowNodePtr* other, bool as_child) {
    if (this->ptr == 0) { //We don't have default
        debug_flow("No node, attaching other");
        *this = (*other);
    } else { //We have default, other have default
        debug_flow("There is a node");
        if (this->is_leaf() && other->is_leaf()) { //Our default is a leaf and other default is a leaf
            this->leaf->combine_data(other->leaf->data);
        } else if (this->is_node()) { //Our default is a node
            this->node_combine_ptr(p, *other, as_child);
        } else { //other default is node, our is leaf
            //We replace all other leaf with our data
            other->node->leaf_combine_data_create(this->leaf, true, true);
            assert(other->node->has_no_default());
            this->set_node(other->node);
        }
    }
    this->set_parent(p);

}

void FlowNode::apply(std::function<void(FlowNodePtr*)>fnt) {
    NodeIterator it = iterator();
    FlowNodePtr* cur = 0;
    while ((cur = it.next()) != 0) {
        fnt(cur);
    }
}

void FlowNode::apply_default(std::function<void(FlowNodePtr*)> fnt) {
    apply(fnt);
    if (_default.ptr) {
        fnt(&_default);
    }
}

/**
 * Prune the tree by adding the knowledge that the given level will or will not (inverted) be of the given value
 * if inverted, it means the level will NOT be data
 */
FlowNodePtr FlowNode::prune(FlowLevel* level,FlowNodeData data, bool inverted)  {
    if (level->is_dynamic()) {
#if DEBUG_CLASSIFIER
        click_chatter("Not pruning a dynamic level");
#endif
        return FlowNodePtr(this);
    }
    if (level->equals(this->level())) { //Same level
        if (inverted) {
            //Remove data from level if it exists
            FlowNodePtr* ptr = find(data);
            assert(this->child_deletable());
            FlowNodePtr child = *ptr;
            ptr->ptr = 0;
            dec_num();
            //TODO delete child

        } else {
            //Return the child
            FlowNodePtr* ptr = find_or_default(data);
            FlowNodePtr child = *ptr;
            dec_num();
            ptr->ptr = 0;
            //TODO delete this;
            return child;
        }
    }

    apply_default([this,level,data,inverted](FlowNodePtr* cur){
        if (cur->is_leaf()) {
            return;
        }
        FlowNodeData old_data = cur->data();
        FlowNodePtr newcur = cur->node->prune(level, data, inverted);
        if (cur->ptr == newcur.ptr) //CHild did not change
            return;
        assert(newcur.ptr != 0);
        if (newcur.is_node()) {
            newcur.node->check();
        }
        *cur = newcur;
        cur->set_data(old_data);
        cur->set_parent(this);
    });
    if (inverted) {
        if (getNum() == 0 && !this->level()->is_dynamic()) {
            //All child values were removed, return the default
            FlowNodePtr def = *default_ptr();
            default_ptr()->ptr = 0;
            delete this;
            return def;
        }
    }
    return FlowNodePtr(this);
}

/**
 * Replace data in all leaf, without creating new ones
 */
void FlowNode::leaf_combine_data(FlowControlBlock* leaf, bool do_final, bool do_default) {
    traverse_all_leaves([leaf](FlowNodePtr* ptr) -> bool {
        ptr->leaf->combine_data(leaf->data);
        return true;
    }, do_final, do_default);
}

/**
 * Replace data in all leaf, creating new ones
 */
void FlowNode::leaf_combine_data_create(FlowControlBlock* leaf, bool do_final, bool do_default) {
    traverse_all_leaves_and_empty_default([leaf](FlowNodePtr* ptr,FlowNode* parent) -> bool {
        if (ptr->leaf == 0) {
            ptr->set_leaf(leaf->duplicate(1));
            ptr->set_parent(parent);
        } else {
            ptr->leaf->combine_data(leaf->data);
        }
        return true;
    }, do_final, do_default);
}

/**
 * Replace a leaf with a node
 */
void FlowNodePtr::replace_leaf_with_node(FlowNode* other) {
    assert(is_leaf());
    assert(ptr);
    FlowNodeData old_data = data();
    FlowNode* old_parent = parent();
    FlowControlBlock* old_leaf = leaf;
    FlowNodePtr no(other->duplicate(true, 1));

#if DEBUG_CLASSIFIER
    click_chatter("Pruning:");
    this->print();
#endif
    //Prune the downward tree with all values of the future new parent
    FlowNode* gparent = old_parent;
    FlowNodeData gdata = old_data;
    bool was_default = old_parent->default_ptr()->ptr == leaf;
    while (gparent != NULL) {
        no = no.node->prune(gparent->level(),gdata);
        if (!no.ptr) { //Completely pruned, keep the FCB as it.
            return;
        }
        if (no.is_leaf()) {
            break;
        }
        gdata = gparent->node_data;
        FlowNode* child = gparent;
        gparent = gparent->parent();
        was_default = !gparent || child == gparent->default_ptr()->node;
    }

    //Replace the pointer by the new
    *this = no;
    set_data(old_data);
    set_parent(old_parent);

    debug_flow("Pruned other : ");
#if DEBUG_CLASSIFIER
    no.print();
#endif
    //Combine FCB data
    if (no.is_leaf())
        no.leaf->combine_data(old_leaf->data);
    else
        no.node->leaf_combine_data(old_leaf,true,true); //We do all here as the downward must be completely updated with our data

    //Release original leaf
    //TODO old_leaf->release(1);
}

/**
 * Replace all leave of this node per another node (that will be deep duplicated for each replacement)
 * Combines FCB values, asserting that they are equals or one is unset
 *
 */
FlowNode* FlowNode::replace_leaves(FlowNode* other, bool do_final, bool do_default)  {
    assert(do_final); //Protect against legacy
    assert(!do_default); //Protect against legacy
        if (other == 0) return this;
        auto fnt = [other](FlowNodePtr* ptr) -> bool {
            assert(ptr != 0);
            assert(ptr->ptr != 0);
            ptr->replace_leaf_with_node(other);
            return true;
        };
        this->traverse_all_leaves(fnt, do_final, do_default);
        //TODO delete other;
        return this;
}

FlowNode* FlowNode::optimize() {
	FlowNodePtr* ptr;

	//Optimize default
	if (_default.ptr && _default.is_node())
		_default.node = _default.node->optimize();

	if (!level()->is_dynamic()) {
		if (getNum() == 0) {
			//No nead for this level
			if (default_ptr()->is_node()) {
				//if (dynamic_cast<FlowNodeDummy*>(this) == 0) {
#if DEBUG_CLASSIFIER
                    click_chatter("Optimize : no need for this level");
#endif
					/*FlowNodeDummy* fl = new FlowNodeDummy();
					fl->assign(this);
					fl->_default = _default;
					_default.ptr = 0;
					delete this;
                    fl->check();
					return fl;*/
                    _default.set_parent(0);
                    return _default.node;
				//}
			} else {
			    //TODO
			    assert(false);
			}
		} else if (getNum() == 1) {
			FlowNode* newnode;

			FlowNodePtr* child = (iterator().next());

			if (_default.ptr == 0) {
				if (child->is_leaf()) {
#if DEBUG_CLASSIFIER
					click_chatter("Optimize : one leaf child and no default value : creating a dummy level !");
					//TODO : can't we set the child directly?
#endif
					FlowNodeDummy* fl = new FlowNodeDummy();
					fl->assign(this);
					fl->set_default(child->optimize());

					newnode = fl;
				} else { //Child is node
				    FlowNodeDefinition* defnode = dynamic_cast<FlowNodeDefinition*>(this);
				    if (defnode->_else_drop) {
				        FlowNodeTwoCase* fl = new FlowNodeTwoCase(child->optimize());
                        fl->assign(this);
                        fl->inc_num();
                        fl->set_default(_default);
                        newnode = fl;
                        _default.ptr = 0;
				    } else {
    #if DEBUG_CLASSIFIER
                        click_chatter(("Optimize : one child ("+defnode->name()+") and no default value : no need for this level !").c_str());
    #endif
                        newnode = child->node;
				    }
				}
			} else {
#if DEBUG_CLASSIFIER
				click_chatter("Optimize : only 2 possible case (value %lu or default %lu)",child->data().data_64,_default.data().data_64);
#endif
				FlowNodeTwoCase* fl = new FlowNodeTwoCase(child->optimize());
				fl->assign(this);
				fl->inc_num();
				fl->set_default(_default);
				newnode = fl;
				_default.ptr = 0;
			}
			child->set_parent(newnode);
			child->ptr = 0;
			dec_num();
			assert(getNum() == 0);
			//TODO delete this;
            newnode->check();
			return newnode;
		} else if (getNum() == 2) {
#if DEBUG_CLASSIFIER
            click_chatter("Optimize : node has 2 childs");
#endif
			FlowNode* newnode;
			NodeIterator cit = iterator();
			FlowNodePtr* childA = (cit.next());
			FlowNodePtr* childB = (cit.next());
			if (childB->else_drop() || !childA->else_drop()) {
			    FlowNodePtr* childTmp = childB;
			    childB = childA;
			    childA = childTmp;
			}

			if (_default.ptr == 0 && !childB->else_drop()) {
#if DEBUG_CLASSIFIER
				click_chatter("Optimize : 2 child and no default value : only 2 possible case (value %lu or value %lu)!",childA->data().data_64,childB->data().data_64);
#endif
				FlowNodePtr newA = childA->optimize();
				FlowNodeTwoCase* fl = new FlowNodeTwoCase(newA);
				fl->inc_num();
				fl->assign(this);
				FlowNodePtr newB = childB->optimize();
				fl->set_default(newB);
				newnode = fl;
			} else {
#if DEBUG_CLASSIFIER
				click_chatter("Optimize : only 3 possible cases (value %lu, value %lu or default %lu)",childA->data().data_64,childB->data().data_64,_default.data().data_64);
#endif
				FlowNodePtr ncA = childA->optimize();
				FlowNodePtr ncB = childB->optimize();
#if DEBUG_CLASSIFIER
				click_chatter("The 2 cases are :");
				ncA.print();
				click_chatter("And :");
				ncB.print();
#endif
				FlowNodeThreeCase* fl = new FlowNodeThreeCase(ncA,ncB);
				fl->inc_num();
				fl->inc_num();
				fl->assign(this);
				fl->set_default(_default);
				newnode = fl;
				_default.ptr = 0;
			}
			childA->set_parent(newnode);
			childB->set_parent(newnode);
			childA->ptr = 0;
			childB->ptr = 0;
			dec_num();
			dec_num();
			assert(getNum() == 0);
			//TODO delete this;
            newnode->check();
			return newnode;
		} else {
#if DEBUG_CLASSIFIER
			click_chatter("No optimization for level with %d childs",getNum());
#endif
			return dynamic_cast<FlowNodeDefinition*>(this)->create_final();
		}
	} else {
#if DEBUG_CLASSIFIER
		click_chatter("Dynamic level won't be optimized");
#endif
		return dynamic_cast<FlowNodeDefinition*>(this)->create_final();
	}

	//Unhandled case?
	assert(false);
	return this;
}

FlowNodePtr FlowNodePtr::optimize() {
    if (is_leaf()) {
        return *this;
    } else {
        FlowNodePtr ptr = *this;
        FlowNodeData data = node->node_data;
        ptr.node = node->optimize();
        ptr.node->node_data = data;
        ptr.node->check();
        return ptr;
    }
}

bool FlowNodePtr::else_drop() {
    if (is_leaf())
        return false;
    else
        return dynamic_cast<FlowNodeDefinition*>(node)->_else_drop;
}

/**
 * True if no data is set in the FCB.
 */
bool FlowControlBlock::empty() {
    for (unsigned i = sizeof(FlowNodeData); i < get_pool()->data_size();i++) {
        if (data[i] != 0) {
            debug_flow("data[%d] = %x is different",i,data[i]);
            return false;
        }
    }
    return true;
}

void FlowControlBlock::print(String prefix, int data_offset, bool show_ptr) const {
	char data_str[64];
	int j = 0;

	if (data_offset == -1) {
        for (unsigned i = 0; i < get_pool()->data_size() && j < 60;i++) {
            sprintf(&data_str[j],"%02x",data[i]);
            j+=2;
        }
	} else {
	    sprintf(&data_str[j],"%02x",data[data_offset]);
	}
	if (show_ptr)
	    click_chatter("%s %lu Parent:%p UC:%d (%p data %s)",prefix.c_str(),node_data[0].data_64,parent,count(),this,data_str);
	else
	    click_chatter("%s %lu UC:%d (data %s)",prefix.c_str(),node_data[0].data_64,count(),data_str);
}

void FlowNodePtr::print() const{
	if (is_leaf())
		leaf->print("");
	else
		node->print();
}


void FlowControlBlock::combine_data(uint8_t* data) {
    /*debug_flow("Combine data for :");
    print("");*/
    for (unsigned i = sizeof(FlowNodeData); i < get_pool()->data_size();i++) {
       if (data[i] == 0)
           continue;
       if (this->data[i] == 0) {
           this->data[i] = data[i];
       } else {
          if (this->data[i] != data[i]) {
              click_chatter("!!!");
              click_chatter("WARNING : OVERWRITTEN CLASSIFICATION !");
              click_chatter("Impossible classification ! Some path merge with different FCB values (%x, %x), hence different decisions!",this->data[i], data[i]);
              click_chatter("WARNING : OVERWRITTEN CLASSIFICATION !");
              click_chatter("!!!");
              print("");
              assert(false);
          }
       }
    }
}


FlowNode* FlowLevel::create_better_node(FlowNode* parent) {
    int l;
    if (dynamic_cast<FlowNodeHash<0>*>(parent) != 0) {
        l = 0;
    } else if (dynamic_cast<FlowNodeHash<1>*>(parent) != 0) {
        l = 1;
    } else if (dynamic_cast<FlowNodeHash<2>*>(parent) != 0) {
        l = 2;
    } else if (dynamic_cast<FlowNodeHash<3>*>(parent) != 0) {
        l = 3;
    } else if (dynamic_cast<FlowNodeHash<4>*>(parent) != 0) {
        l = 4;
    } else if (dynamic_cast<FlowNodeHash<5>*>(parent) != 0) {
        l = 5;
    } else if (dynamic_cast<FlowNodeHash<6>*>(parent) != 0) {
        l = 6;
    } else if (dynamic_cast<FlowNodeHash<7>*>(parent) != 0) {
        l = 7;
    } else if (dynamic_cast<FlowNodeHash<8>*>(parent) != 0) {
        l = 8;
    } else if (dynamic_cast<FlowNodeHash<9>*>(parent) != 0) {
        l = 9;
    } else {
        l = -1;
    }

    if (l == 9) {
        return 0;
    }
    ++l;
    if (l > current_level)
        current_level = l;

    return FlowNode::create_hash(current_level);
}

FCBPool* FCBPool::biggest_pool = 0;
int NR_SHARED_FLOW = 0;

#endif

CLICK_ENDDECLS
