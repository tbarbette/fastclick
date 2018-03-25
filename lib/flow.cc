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
    assert(_classifier_release_fnt);
    _root = node;
/*#if HAVE_DYNAMIC_FLOW_RELEASE_FNT
    auto fnt = [this](FlowControlBlock* fcb){
        fcb->release_fnt = _classifier_release_fnt;
    };
    node->traverse<decltype(fnt)>(fnt);
#endif*/
}

FlowNode* FlowClassificationTable::get_root() {
    return _root;
}

FlowTableHolder::FlowTableHolder() :
#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
old_flows(fcb_list()),
#endif
_pool(), _classifier_release_fnt(0), _classifier_thunk(0)
    {

    }

#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
void
FlowTableHolder::delete_all_flows() {
    for (int i = 0; i < old_flows.weight(); i++) {
        fcb_list& head = old_flows.get_value(i);
        FlowControlBlock* next = 0;
        FlowControlBlock* b = head._next;
        FlowControlBlock** prev = &head._next;

        #if DEBUG_CLASSIFIER_TIMEOUT_CHECK || DEBUG_CLASSIFIER_TIMEOUT
            assert(head.count() == head.find_count());
        #endif
        while (b != 0) {
            next = b->next;
            b->flags = 0;
            *prev = b->next;
            head._count--;
            b->_do_release();
            b = next;
        }
    }
}
#endif

FlowTableHolder::~FlowTableHolder() {
#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
    delete_all_flows();
#endif
}



void FlowTableHolder::set_release_fnt(SubFlowRealeaseFnt pool_release_fnt, void* thunk) {
	_classifier_release_fnt = pool_release_fnt;
	_classifier_thunk = thunk;
}

FlowClassificationTable::~FlowClassificationTable() {
        bool previous = pool_allocator_mt_base::dying();
        pool_allocator_mt_base::set_dying(true);
        if (_root)
            delete _root;
        _root = 0;
        pool_allocator_mt_base::set_dying(previous);
}

FlowClassificationTable::Rule FlowClassificationTable::make_ip_mask(IPAddress dst, IPAddress mask) {
    FlowLevelGeneric32* fl = new FlowLevelGeneric32();
    fl->set_match(offsetof(click_ip,ip_dst),mask.addr());
    FlowNodeDefinition* node = new FlowNodeDefinition();
    node->_level = fl;
    node->_parent = 0;
    FlowControlBlock* fcb = FCBPool::init_allocate();
    FlowNodeData ip = FlowNodeData(dst.addr());
    FlowNodePtr* parent_ptr = node->find(ip);
    parent_ptr->set_leaf(fcb);
    parent_ptr->set_data(ip);
    parent_ptr->leaf->parent = node;
    parent_ptr->leaf->acquire(1);
    node->inc_num();
    return Rule{.root=node};

}

FlowClassificationTable::Rule FlowClassificationTable::parse(String s, bool verbose) {
    std::regex reg("((?:(?:(?:agg|thread|(?:(?:ip)?[+-]?[0-9]+/[0-9a-fA-F]+?/?[0-9a-fA-F]+?))(?:[:]HASH-[0-9]+|[:]ARRAY)?[!]? ?)+)|-)( keep)?( [0-9]+| drop)?",
             std::regex_constants::icase);
    std::regex classreg("thread|agg|(ip[+])?([-]?[0-9]+)/([0-9a-fA-F]+)?/?([0-9a-fA-F]+)?([:]HASH-[0-9]+|[:]ARRAY)?([!])?",
                 std::regex_constants::icase);

    FlowNode* root = 0;

    FlowNodePtr* parent_ptr = 0;

    bool deletable_value = false;
    int output = 0;
    bool is_default = false;

    std::smatch result;
    std::string stdstr = std::string(s.c_str());

    if (std::regex_match(stdstr, result, reg)){
        FlowNodeData lastvalue = FlowNodeData((uint64_t)0);

        std::string classs = result.str(1);
        std::string keep = result.str(2);
        std::string deletable = result.str(3);
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
        if (classs != "-") {


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
                std::string hint = it->str(5);
                std::string important = it->str(6);


                if (verbose)
                    click_chatter("o : %s, v : %s, m : %s",offset.c_str(),value.c_str(), mask.c_str());
                FlowLevel* f;
                bool dynamic = false;
                unsigned long valuev = 0;
                if (classs == "agg" || classs=="AGG") {
                    FlowLevelAggregate* fl = new FlowLevelAggregate();
                    /*if (offset == "") {
                        fl->offset = 0;
                    } else {
                        fl->offset = std::stoul(offset);
                    }
                    fl->mask = maskv;*/
                    f = fl;
                    if (verbose)
                        click_chatter("AGG");
                    dynamic = true;
                } else if (classs == "thread" || classs == "THREAD") {
                    FlowLevelThread* fl = new FlowLevelThread(click_max_cpu_ids());
                    f = fl;
                    click_chatter("THREAD");
                    dynamic = true;
                } else  {

                    unsigned long maskv = 0xffffffff;
                    valuev = 0xffffffff;
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
                        maskv = (1ul << value.length() * 4) - 1;

                    //TODO error for > 64

                    if (maskv <= UINT8_MAX){
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
                    if (maskv & valuev == 0)
                        dynamic = true;
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

                if (dynamic) { //If a mask is provided, value is dynamic
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

        FlowControlBlock* fcb = FCBPool::init_allocate();
        parent_ptr->set_leaf(fcb);
        parent_ptr->set_data(lastvalue);
        parent_ptr->leaf->parent = parent;
        parent_ptr->leaf->acquire(1);


        root->check();
    } else {
        click_chatter("%s is not a valid rule",s.c_str());
        abort();
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

void FlowNodePtr::node_combine_ptr(FlowNode* parent, FlowNodePtr other, bool as_child, bool priority) {
    if (other.is_leaf()) {
        assert(!as_child);

        auto fnt = [other,as_child,priority](FlowNode* parent) -> bool {
            if (parent->default_ptr()->ptr == 0) {
                parent->default_ptr()->set_leaf(other.leaf->duplicate(1));
                parent->default_ptr()->set_parent(parent);
            } else {
                if (as_child || !priority) {
                    FlowNodeData data = parent->default_ptr()->data();
                    parent->default_ptr()->leaf->release();
                    parent->default_ptr()->set_leaf(other.leaf->duplicate(1));
                    parent->default_ptr()->leaf->parent = parent;
                    parent->default_ptr()->set_data(data);
                } else {
                    parent->default_ptr()->leaf->combine_data(other.leaf->data);
                    if (parent->default_ptr()->leaf->is_early_drop() && !other.leaf->is_early_drop())
                        parent->default_ptr()->leaf->set_early_drop(false);
                }
            }
            return true;
        };
        this->node->traverse_all_default_leaf(fnt);
    } else {
        FlowNode* new_node = node->combine(other.node, as_child, priority);
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
 * Combine this rule with another rule
 * @arg other A node to merge
 * @arg as_child
 *   - If true :merge the rule as a child of this one, ie REMOVING all child
 *    leaves (data is lost) and changing them by other.
 *   - If false : merge as the "else" path, ie appending other to all default path.
 * @arg priority : Should this be considered as with priority than other. If not, rule
 *  may be exchanged for optimization
 *
 */
FlowNode* FlowNode::combine(FlowNode* other, bool as_child, bool priority) {
    //TODO : priority is not used as of now, it is always assumed true. We could relax some things.
	if (other == 0) return this;
    other->check();
    this->check();

	assert(other->parent() == 0);

	//Remove useless level (case where this is dummy)
	if (dynamic_cast<FlowLevelDummy*>(this->level()) != 0) {
	        debug_flow("COMBINE (as child : %d, default is leaf : %d) : I am dummy",as_child,_default.is_leaf());
	        if (_default.is_leaf()) {
	            other->leaf_combine_data(_default.leaf, as_child, !as_child && priority);
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

	//Remove useless level (case where other is dummy)
	if (dynamic_cast<FlowLevelDummy*>(other->level()) != 0) { //Other is dummy :
	    debug_flow("COMBINE : Other is dummy")
	    //If other is a dummy (and we're not)
	    if (other->_default.is_leaf()) {
	        debug_flow("COMBINE : Other is a leaf (as child %d):",as_child)
	        //other->_default.leaf->print("");
	        if (as_child) { //Combine a leaf as child of all our leaf
	            this->leaf_combine_data(other->_default.leaf, true, true);
	        } else { //Combine a leaf as "else" of all our default routes
	            /*
	             * Duplicate the leaf for all null default routes and ED routes if priority is false (and as_child is false)
	             */
                auto fnt = [other,priority](FlowNode* parent) -> bool {
                    if (parent->default_ptr()->ptr == 0) {
                        parent->default_ptr()->set_leaf(other->_default.leaf->duplicate(1));
                        parent->default_ptr()->set_parent(parent);
                    } else if (!priority && parent->default_ptr()->leaf->is_early_drop()) {
                        FCBPool::init_release(parent->default_ptr()->leaf);
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

    flow_assert(!is_dummy());
    flow_assert(!other->is_dummy());
    if (this->level()->is_dynamic() && !other->level()->is_dynamic()) {
        if (priority) {
            click_chatter("Trying to attach a non-dynamic child to a dynamic node. This generally means you need a new FlowClassifier after a dynamic node, such as TCPIn elements or flow-based one");
            this->print();
            other->print();
            abort();
        } else {
            this->set_parent(0);
            debug_flow("Combining a dynamic parent with a non dynamic node. As Priority is false, we invert the child and the parent");
            this->debug_print();
            other->debug_print();
            FlowNodeData d = this->node_data;
            FlowNode* o = other->combine(this, as_child, false); //Priority is false in this path
            o->node_data = d;
            return o;
        }
    }

	if (as_child)
	    __combine_child(other, priority);
	else
	    __combine_else(other, priority);
	return this;
}

void FlowNode::__combine_child(FlowNode* other, bool priority) {
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
			click_chatter("COMBINE-CHILD : taking child %lu",other_child_ptr->data().data_64);
#endif

            FlowNodePtr* child_ptr = find(other_child_ptr->data());
            if (child_ptr->ptr == 0) { //We have no same data, so we just append the other's child to us
                *child_ptr = *other_child_ptr;
                child_ptr->set_parent(this);
                inc_num();
            } else {

			    //There is some data in our child that is the same as the other child
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
					child_ptr->node = child_ptr->node->combine(other_child_ptr->node,true,priority);
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
            this->default_ptr()->default_combine(this, other->default_ptr(), true, priority);
            other->default_ptr()->ptr = 0;
        } //ELse nothing to do as we can keep our default as it, if any*/
		//TODO delete other;
        this->check();
		return;
	}

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
            this->replace_leaves(other, true, false, true);//Discard child FCB always as we are in the as_child path
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
void FlowNode::__combine_else(FlowNode* other, bool priority) {

    if (level()->equals(other->level())) { //Same level
        if (other->level()->is_dynamic()) {
            level()->set_dynamic();
        }
        debug_flow("COMBINE-ELSE : same level");
        FlowNode::NodeIterator other_childs = other->iterator();

        /**
         * We add each of the other child to us.
         * - If the child does not exist, we add ourself simply as a child
         * - If the child exist, we combine the item with the child
         */
        FlowNodePtr* other_child_ptr;
        while ((other_child_ptr = other_childs.next()) != 0) { //For each child of the other node
            debug_flow("COMBINE-ELSE : taking child %lu",other_child_ptr->data().data_64);

            FlowNodePtr* child_ptr = find(other_child_ptr->data());
            if (child_ptr->ptr == 0) { //We have no same data, so we just append the other's child to us, merging it with a dup of our default
                if (_default.ptr) {
                    debug_flow("Our child is empty, duplicating default (is_leaf : %d)",_default.is_leaf());
                    if (_default.is_leaf()) {
                        child_ptr->set_leaf(_default.leaf->duplicate(1));
                        child_ptr->set_data(other_child_ptr->data());
                    } else {
                        child_ptr->set_node(_default.node->duplicate(true,1));
                        child_ptr->set_data(other_child_ptr->data());
                    }
                    child_ptr->set_parent(this);
                    inc_num();

                    goto attach;
                } else {
                    *child_ptr = *other_child_ptr;
                    child_ptr->set_parent(this);
                    inc_num();
                }
            } else {
                attach:
                //There is some data in our child that is the same as the other child
                if (child_ptr->is_leaf() && other_child_ptr->is_leaf()) {
                    //DO nothing, this is a else rule, mine take precedence
                } else if (child_ptr->is_node() && other_child_ptr->is_node()) {
                    //So we combine our child node with the other child node
                    //We must set the parent to null, so the combiner nows he can play with that node
                    other_child_ptr->node->set_parent(0);
                    child_ptr->node = child_ptr->node->combine(other_child_ptr->node,false,priority);
                    child_ptr->node->set_parent(this);
                } else if (child_ptr->is_leaf() && other_child_ptr->is_node()) {
                    other_child_ptr->node->leaf_combine_data_create(child_ptr->leaf, true, true, false);
                    child_ptr->set_node(other_child_ptr->node);
                    child_ptr->node->set_parent(this);
                } else {
                    click_chatter("ERROR when merging :");
                    other_child_ptr->print();
                    child_ptr->print();
                    click_chatter("Their parents are :");
                    other->print();
                    this->print();
                    click_chatter("There are probably merging paths");
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
            other->default_ptr()->set_parent(0);
            this->default_ptr()->default_combine(this, other->default_ptr(), false, priority);
            other->default_ptr()->ptr = 0;
        } //ELse nothing to do as we can keep our default as it, if any
        //TODO : delete other;
        this->check();
        return;
    }

    debug_flow("Mhh... No easy combine. Combining other to all children and default (priority %d, as_child 0)",priority);
    //In other terms, if the packet does not match any of the rules, it will go through "other"
    this->debug_print();
    other->debug_print();

    NodeIterator it = iterator();
    FlowNodePtr* cur;
    FlowNodePtr Vpruned_default(other->duplicate(true, 1));
    while ((cur = it.next()) != 0) {
        if (cur->is_node()) {
            bool changed;
            cur->node_combine_ptr(this, other->duplicate(true, 1)->prune(level(), cur->data(), false, changed),false, priority);
        } else {
            if (Vpruned_default.is_node()) { //Other is guaranteed to be not null here
                bool changed;
                FlowNodeData d = Vpruned_default.data();
                Vpruned_default = Vpruned_default.node->prune(level(), cur->data(), true, changed);
            }
        }
    }
#if DEBUG_CLASSIFIER
    debug_flow("Pruned other default :");
    Vpruned_default.print();
    debug_flow("my default:");
    if (this->default_ptr()->ptr != 0)
        this->default_ptr()->print();
#endif
    this->default_ptr()->default_combine(this, &Vpruned_default, false, priority);
    //TODO : delete other
#if DEBUG_CLASSIFIER
    debug_flow("Result of no easy combine :");
    this->print();
    this->check();
#endif
    return;
}

/**
 * Correct iif this FLowNodePtr is a default ptr.
 *
 * Will combine this default pointer with whatever other is.
 * - No need to set the data of the child as we are default
 * - Parent is corrected at the end of the function
 */
void FlowNodePtr::default_combine(FlowNode* p, FlowNodePtr* other, bool as_child, bool priority) {
    if (this->ptr == 0) { //We don't have default
        debug_flow("No node, attaching other");
        *this = (*other);
    } else { //We have default, other have default
        debug_flow("We have a node or a leaf %d %d , p %d",this->is_leaf(),other->is_leaf(),priority);
        if (this->is_leaf() && other->is_leaf()) { //Our default is a leaf and other default is a leaf
            if (!priority) {
                FCBPool::init_release(this->leaf);
                this->leaf = other->leaf;
            } else {
                this->leaf->combine_data(other->leaf->data);
                if (this->leaf->is_early_drop() && !other->leaf->is_early_drop())
                    this->leaf->set_early_drop(false);
            }
        } else if (this->is_node()) { //Our default is a node, other is a leaf or a node
            this->node_combine_ptr(p, *other, as_child, priority);
        } else { //other default is node, our is leaf
            //We replace all other leaf with our data
            other->node->leaf_combine_data_create(this->leaf, true, true, !priority || other->node->level()->is_dynamic());
            flow_assert(other->node->has_no_default());
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
FlowNodePtr FlowNode::prune(FlowLevel* olevel,FlowNodeData data, bool inverted, bool &changed)  {
    if (is_dummy()) { //If we are dummy, we cannot prune
        return FlowNodePtr(this);
    }

    FlowNodePtr ptr(this);

    debug_flow("Prune level %s(dyn%d) with %s(dyn%d), i %d, data %llu",
            level()->print().c_str(),level()->is_dynamic(),
            olevel->print().c_str(),olevel->is_dynamic(),
            inverted,
            data.data_64);

    if (level()->is_dynamic()) { //If we are dynamic, we can remove from our mask the mask of the second value
        debug_flow("Pruning a dynamic level...");
        if (inverted && !olevel->is_dynamic()) {//We're in the default path, nothing to remove as it won't help that we know we won't see some static value
            debug_flow("Inverted...");
        } else { //Not inverted (we will see the given value), or inverted but other is a dynamic value meaning that the bits of level will be known when reaching this value
/*            if ((olevel->is_dynamic() && !inverted)) {//We would be in the child of a dynamic level that is not on a defautl path
                print();
                assert(false);
            }*/ //ALLOWED FOR VERIF-PRUNING
            /**
             * If other is dynamic or not does not matter, we will have those bits fixed, so we remove it from our dynamic mask
             */
            if (this->level()->prune(olevel)) {
                changed = true;
                if (!this->level()->is_usefull()) {
                    debug_flow("Not usefull anymore, returning default !");
                    assert(this->getNum() == 0);
                    ptr = _default;
                } else {
                    assert(this->getNum() == 0);
                    /*ptr.node->apply_default([ptr,olevel,data,inverted,&changed](FlowNodePtr* cur){
                        if (cur->is_leaf()) {
                            return;
                        }
                        FlowNodeData old_data = cur->data();
                        FlowNodePtr newcur = cur->node->prune(olevel, data, inverted, changed);
                        if (cur->ptr == newcur.ptr) //CHild did not change
                            return;
                        changed = true;
                        assert(newcur.ptr != 0);
                        if (newcur.is_node()) {
                            newcur.node->check();
                        }
                        *cur = newcur;
                        cur->set_data(old_data);
                        cur->set_parent(ptr.node);
                    });*/
                }
            }
        }

    } else {
        debug_flow("Pruning a static level...");
        if (olevel->is_dynamic()) {
            //At this time the value will be known... But that does not help us
            click_chatter("Static child of a dynamic parent.");
            assert(false);
        } else {
            if (inverted) {
                if (olevel->equals(this->level())) { //Same level
                    //Remove data from level if it exists
                    FlowNodePtr* ptr_child = find(data);
#if FLOW_KEEP_STRUCTURE
                    assert(this->child_deletable());
#endif
                    FlowNodePtr child = *ptr_child;
                    ptr_child->ptr = 0;
                    dec_num();
                    changed = true;
                    //TODO delete child
                }
            } else {
                ptr = _level->prune(olevel, data, this, changed);
            }
        }
        /*if (level->equals(this->level())) { //Same level
            if (inverted) {
                //Remove data from level if it exists
                FlowNodePtr* ptr = find(data);
                assert(this->child_deletable());
                FlowNodePtr child = *ptr;
                ptr->ptr = 0;
                dec_num();
                changed = true;
                //TODO delete child

            } else {
                //Return the child
                FlowNodePtr* ptr = find_or_default(data);
                FlowNodePtr child = *ptr;
                dec_num();
                ptr->ptr = 0;
                //TODO delete this;
                changed = true;
                return child;
            }
        }*/
    }

    if (ptr.is_leaf()) {
        return ptr;
    }
    /**
     * Prune all child node including default
     */
    ptr.node->apply_default([ptr,olevel,data,inverted,&changed](FlowNodePtr* cur){
        if (cur->is_leaf()) {
            return;
        }
        FlowNodeData old_data = cur->data();
        FlowNodePtr newcur = cur->node->prune(olevel, data, inverted, changed);
        if (cur->ptr == newcur.ptr) //CHild did not change
            return;
        changed = true;
        assert(newcur.ptr != 0);
        if (newcur.is_node()) {
            newcur.node->check();
        }
        *cur = newcur;
        cur->set_data(old_data);
        cur->set_parent(ptr.node);
    });
    /**
     * If inverted and there is no more children, remove the node
     */
    if (inverted) {
        if (getNum() == 0 && !this->level()->is_dynamic()) {
            //All child values were removed, return the default
            FlowNodePtr def = *default_ptr();
            default_ptr()->ptr = 0;
            changed = true;
            delete this;
            return def;
        }
    }
    return ptr;
}

/**
 * Replace data in all leaf, without creating new ones.
 */
void FlowNode::leaf_combine_data(FlowControlBlock* leaf, bool do_final, bool do_default) {
    traverse_all_leaves([leaf](FlowNodePtr* ptr) -> bool {
        ptr->leaf->combine_data(leaf->data);
        if (ptr->leaf->is_early_drop() && !leaf->is_early_drop())
            ptr->leaf->set_early_drop(false);
        return true;
    }, do_final, do_default);
}

/**
 * Replace data in all leaf, creating new ones
 */
void FlowNode::leaf_combine_data_create(FlowControlBlock* leaf, bool do_final, bool do_default, bool discard_my_fcb_data) {
    traverse_all_leaves_and_empty_default([leaf,discard_my_fcb_data](FlowNodePtr* ptr,FlowNode* parent) -> bool {
        if (ptr->leaf == 0) {
            ptr->set_leaf(leaf->duplicate(1));
            ptr->set_parent(parent);
        } else {
            if (!discard_my_fcb_data) {
                ptr->leaf->combine_data(leaf->data);
                if (ptr->leaf->is_early_drop() && !leaf->is_early_drop())
                    ptr->leaf->set_early_drop(false);
            }
        }
        return true;
    }, do_final, do_default);
}

/**
 * Replace a leaf with a node. Return true if
 * data from the parent allowed to prune the child (eg, the child contained a match for
 * tcp port 80, but our parent already had it, or already cassify in a tcp port other than 80, in which case
 * other is completely killed.
 */
bool FlowNodePtr::replace_leaf_with_node(FlowNode* other, bool discard) {
    assert(is_leaf());
    assert(ptr);
    bool changed = false;
    FlowNodeData old_data = data();
    FlowNode* old_parent = parent();
    FlowControlBlock* old_leaf = leaf;
    FlowNodePtr no(other->duplicate(true, 1));

    flow_assert(other->is_full_dummy() || !other->is_dummy());
#if DEBUG_CLASSIFIER
    click_chatter("Replacing leaf");
    print();
    click_chatter("Of:");
    if (this->parent() and this->parent()->parent())
        this->parent()->root()->print();
    else if (this->parent())
        this->parent()->print();
    else
        this->print();
    click_chatter("With other :");
    no.print();
#endif
    //Prune the downward tree with all values of the future new parent
    FlowNode* gparent = old_parent;
    FlowNodeData gdata = old_data;
    bool was_default = old_parent->default_ptr()->ptr == leaf;
    while (gparent != NULL) {
        no = no.node->prune(gparent->level(),gdata, was_default, changed);
        if (!no.ptr) { //Completely pruned, keep the FCB as it.
            debug_flow("Completely pruned");
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

    //Replace the pointer by the new
    *this = no;
    set_data(old_data);
    set_parent(old_parent);

#if DEBUG_CLASSIFIER
    if (changed) {
        debug_flow("Pruned other : ");
        no.print();
    } else {
        debug_flow("Pruning did not change the node.");
    }
#endif
    //Combine FCB data
    if (!discard) {
        if (is_leaf()) {
            leaf->combine_data(old_leaf->data);
            if (leaf->is_early_drop() && !old_leaf->is_early_drop())
                leaf->set_early_drop(false);
        } else {
            node->leaf_combine_data(old_leaf,true,true); //We do all here as the downward must be completely updated with our data
        }
    }

    return changed;
    //Release original leaf
    //TODO old_leaf->release(1);
}

/**
 * Replace all leave of this node per another node (that will be deep duplicated for each replacement)
 * Combines FCB values, asserting that they are equals or one is unset
 *
 */
FlowNode* FlowNode::replace_leaves(FlowNode* other, bool do_final, bool do_default, bool discard_my_fcb_data)  {
    assert(do_final); //Protect against legacy
    assert(!do_default); //Protect against legacy
        if (other == 0) return this;
        flow_assert(!other->is_dummy());
        auto fnt = [other,discard_my_fcb_data](FlowNodePtr* ptr) -> bool {
            assert(ptr != 0);
            assert(ptr->ptr != 0);
            ptr->replace_leaf_with_node(other, discard_my_fcb_data);
            return true;
        };
        this->traverse_all_leaves(fnt, do_final, do_default);
        //TODO delete other;
        return this;
}

/**
 * Optimize table, changing nodes perf the appropriate data structure, removinf useless classification step entries
 * If the path is not mt-safe but reaches a non-mutable level (eg dynamic), a thread node will be added
 */
FlowNode* FlowNode::optimize(bool mt_safe) {
	FlowNodePtr* ptr;

	//Before everything else, remove this level if it's dynamic but useless
    if (level()->is_dynamic() && !level()->is_usefull()) {
        assert(getNum() == 0);
        //No nead for this level
        if (default_ptr()->is_node()) {
#if DEBUG_CLASSIFIER
                click_chatter("Optimize : no need for this dynamic level");
#endif
                _default.set_parent(0);
                return _default.node->optimize(mt_safe);
        } else {
            click_chatter("WARNING : useless path, please specify this to author");
        }
    }

    if (level()->is_mt_safe()) {
        mt_safe = true;
    }

    _level = level()->optimize();

	if (level()->is_dynamic() && !mt_safe) {
	    FlowLevel* thread = new FlowLevelThread(click_max_cpu_ids());
	    FlowNodeArray* fa = FlowAllocator<FlowNodeArray>::allocate();
        fa->initialize(thread->get_max_value());
        fa->_level = thread;
        fa->_parent = parent();
	    fa->set_default(this->optimize(true));
	    return fa;
	}

	//Optimize default
	if (_default.ptr && _default.is_node())
		_default.node = _default.node->optimize(mt_safe);

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
                click_chatter("Non dynamic level, without child that has a leaf as ptr");
                this->print();
                click_chatter("Parent:");
                this->parent()->print();
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
					fl->set_default(child->optimize(mt_safe));

					newnode = fl;
				} else { //Child is node
				    FlowNodeDefinition* defnode = dynamic_cast<FlowNodeDefinition*>(this);
				    if (defnode->_else_drop) {
				        FlowNodeTwoCase* fl = new FlowNodeTwoCase(child->optimize(mt_safe));
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
				FlowNodeTwoCase* fl = new FlowNodeTwoCase(child->optimize(mt_safe));
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
				FlowNodePtr newA = childA->optimize(mt_safe);
				FlowNodeTwoCase* fl = new FlowNodeTwoCase(newA);
				fl->inc_num();
				fl->assign(this);
				FlowNodePtr newB = childB->optimize(mt_safe);
				fl->set_default(newB);
				newnode = fl;
			} else {
#if DEBUG_CLASSIFIER
				click_chatter("Optimize : only 3 possible cases (value %lu, value %lu or default %lu)",childA->data().data_64,childB->data().data_64,_default.data().data_64);
#endif
				FlowNodePtr ncA = childA->optimize(mt_safe);
				FlowNodePtr ncB = childB->optimize(mt_safe);
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
			return dynamic_cast<FlowNodeDefinition*>(this)->create_final(mt_safe);
		}
	} else {
#if DEBUG_CLASSIFIER
		click_chatter("Dynamic level won't be optimized");
#endif
		return dynamic_cast<FlowNodeDefinition*>(this)->create_final(mt_safe);
	}

	//Unhandled case?
	assert(false);
	return this;
}

FlowNodePtr FlowNodePtr::optimize(bool mt_safe) {
    if (is_leaf()) {
        return *this;
    } else {
        FlowNodePtr ptr = *this;
        FlowNodeData data = node->node_data;
        ptr.node = node->optimize(mt_safe);
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
	char data_str[(get_pool()->data_size()*2)+1];
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
	    click_chatter("%s %lu Parent:%p UC:%d ED:%d (%p data %s)",prefix.c_str(),node_data[0].data_64,parent,count(),is_early_drop(),this,data_str);
	else
	    click_chatter("%s %lu UC:%d ED:%d (data %s)",prefix.c_str(),node_data[0].data_64,count(),is_early_drop(),data_str);
}

void FlowControlBlock::reverse_print() {
    FlowNode* p = parent;
    String prefix = "";
    print(prefix);
    if (parent)
        parent->reverse_print();
}


void FlowNodePtr::print() const{
	if (is_leaf())
		leaf->print("");
	else
		node->print();
}


void FlowControlBlock::combine_data(uint8_t* data) {
    for (unsigned i = sizeof(FlowNodeData); i < get_pool()->data_size();i++) {
       if (data[i + FCBPool::init_data_size()] == 0)
           continue;
       if (this->data[i + FCBPool::init_data_size()] == 0) {
           this->data[i] = data[i];
           this->data[i + FCBPool::init_data_size()] = 0xff;
       } else {
          if (this->data[i] != data[i]) {
              click_chatter("!!!");
              click_chatter("WARNING : OVERWRITTEN CLASSIFICATION !");
              click_chatter("Impossible classification ! Some path merge with different FCB values (%x, %x at offset %d), hence different decisions!",this->data[i], data[i],i);
              click_chatter("WARNING : OVERWRITTEN CLASSIFICATION !");
              click_chatter("!!!");
              print("");
              assert(false);
          }
       }
    }
}


FlowNode* FlowLevel::create_node(FlowNode* parent, bool better, bool better_impl) {
    int l;
    if (better) {
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
            //Not a hash
            click_chatter("I don't know how to grow non-hash yet.");
            abort();
        }

        if (l == 9) {
            return 0;
        }
        ++l;
        if (l < current_level) { //TODO keep this as an aggressive mode
            l = current_level;
        }
    }
    else
    {
        l = current_level;
    }

    if (l >= 100 || FlowNodeHash<0>::capacity_for(l) >= this->get_max_value()) {
        FlowNodeArray* fa = FlowAllocator<FlowNodeArray>::allocate();
        fa->initialize(get_max_value());
        l = 100;
        return fa;
    }

    if (l > current_level)
        current_level = l;

    return FlowNode::create_hash(current_level);
}

/**
 * Function not to be called at runtime ! Directly allocate
 * the FCB using the right pool.
 */
FlowControlBlock* FlowControlBlock::duplicate(int use_count) {
    FlowControlBlock* fcb;
    assert(FCBPool::initialized > 0);
    //fcb = get_pool()->allocate();
    fcb = FCBPool::init_allocate();
    //fcb->release_ptr = release_ptr;
    //fcb->release_fnt = release_fnt;
    memcpy(fcb, this ,sizeof(FlowControlBlock) + (get_pool()->data_size() * 2));
    fcb->use_count = use_count;
    return fcb;
}

void FCBPool::compress(Bitvector threads) {
    lists.compress(threads);
    for (unsigned i = 0; i < lists.weight(); i++) {
        SFCBList &list = lists.get_value(i);
        for (int j = 0; j < SFCB_POOL_SIZE; j++) {
            FlowControlBlock* fcb = alloc_new();
            list.add(fcb);
        }
    }
}

FlowControlBlock*
FCBPool::init_allocate() {
       FlowControlBlock* initfcb = (FlowControlBlock*)CLICK_LALLOC(sizeof(FlowControlBlock) + (init_data_size() * 2));
       initfcb->initialize();
       bzero(initfcb->data, (init_data_size() * 2));
       return initfcb;
}

void
FCBPool::init_release(FlowControlBlock* fcb) {
       CLICK_LFREE(fcb,sizeof(FlowControlBlock) + (init_data_size() * 2));
}



FCBPool* FCBPool::biggest_pool = 0;
int FCBPool::initialized = 0;
int NR_SHARED_FLOW = 0;

#endif

CLICK_ENDDECLS
