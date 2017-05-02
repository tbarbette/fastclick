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

const int FlowNodeHash::HASH_SIZES_NR = 10;
const int FlowNodeHash::hash_sizes[FlowNodeHash::HASH_SIZES_NR] = {257,521,1031,2053,4099,8209,16411,32771,65539,131072}; //Prime for less collisions, after the last we double
//const int FlowNodeHash::hash_sizes[FlowNodeHash::HASH_SIZES_NR] = {256,512,1024,2048,4096,8192};


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
#if HAVE_DYNAMIC_FLOW_RELEASE_FNT
    auto fnt = [this](FlowControlBlock* fcb){fcb->release_fnt = _pool_release_fnt;};
    node->traverse<decltype(fnt)>(fnt);
#endif
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

    std::regex reg("((agg )?((?:(?:ip)?[+-]?[0-9]+/[0-9a-fA-F]+?/?[0-9a-fA-F]+?[!]? ?)+)|-)( keep| [0-9]+| drop)?",
             std::regex_constants::icase);
    std::regex classreg("(ip)?[+]?([-]?[0-9]+)/([0-9a-fA-F]+)?/?([0-9a-fA-F]+)?([!])?",
                 std::regex_constants::icase);

    FlowNode* root = 0;

    FlowNodePtr* parent_ptr = 0;

    bool deletable_value = false;
    int output = 0;

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
        std::string deletable = result.str(4);

        if (deletable == " keep")
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
                std::string important = it->str(5);

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
                node->_level = f;
                node->_child_deletable = f->is_deletable();
                node->_parent = parent;
                if (important == "!") {
                    click_chatter("Important !");
                    node->_else_drop = true;
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
            fl->_child_deletable = f->is_deletable();
            fl->_parent = root;
            root = fl;
            parent = root;
            //click_chatter("Default node to output %d",output);
            parent_ptr = root->default_ptr();
        }

        parent_ptr->set_leaf(get_pool()->allocate());
        parent_ptr->set_data(lastvalue);
        parent_ptr->leaf->parent = parent;
        parent_ptr->leaf->acquire(1);


        root->check();
    }
    return FlowClassificationTable::Rule{.root = root, .output = output};
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
    unsigned count = 0;
    FlowControlBlock* b = head.next;
    while (b!= 0) {
        count++;
        b = b->next;
    }
    assert(count == head.count);
#endif
    fcb->next = head.next;
    head.next = fcb;
    head.count++;
    fcb->flags |= FLOW_TIMEOUT_INLIST;

}

bool FlowTableHolder::check_release() {
    fcb_list& head = old_flows.get();

    FlowControlBlock* b = head.next;
    FlowControlBlock** prev = &head.next;
    Timestamp now = Timestamp::recent_steady();
    bool released_something = false;

#if DEBUG_CLASSIFIER_TIMEOUT_CHECK
    unsigned count = 0;
    FlowControlBlock* bc = head.next;
    while (bc!= 0) {
        count++;
        bc = bc->next;
    }
    assert(count == head.count);
#endif

#if DEBUG_CLASSIFIER_TIMEOUT_CHECK || DEBUG_CLASSIFIER_TIMEOUT
    unsigned orig_count = head.count;
    unsigned check_count = 0;
#endif
    while (b != 0) {
        if (b->count() > 0) {
#if DEBUG_CLASSIFIER_TIMEOUT > 2
            click_chatter("FCB %p not releasable anymore as UC is %d",b,b->count());
#endif
            released_something = true;
            b->flags &= ~FLOW_TIMEOUT_INLIST;
            *prev = b->next;
            head.count--;
        }  else if (b->timeoutPassed(now)) {
#if DEBUG_CLASSIFIER_TIMEOUT > 2
            click_chatter("FCB %p has passed timeout",b);
#endif
            released_something = true;
            b->flags = 0;
            b->_do_release();
            *prev = b->next;
            head.count--;
        } else {
            unsigned t = (b->flags >> FLOW_TIMEOUT_SHIFT);
#if DEBUG_CLASSIFIER_TIMEOUT > 1
            click_chatter("Time passed : %d/%d",(now - b->lastseen).msecval(),t);
#endif
            prev = &b->next;
#if DEBUG_CLASSIFIER_TIMEOUT_CHECK > 0
            check_count++;
#endif
        }
        b = b->next;
    }
#if DEBUG_CLASSIFIER_TIMEOUT > 0
    click_chatter("Released  %d->%d==%d",orig_count,head.count,check_count);
#endif
#if DEBUG_CLASSIFIER_TIMEOUT_CHECK > 0
    assert(check_count == head.count);
#endif
    return released_something;
}
#endif
/*******************************
 * FlowNode
 *******************************/

/**
 * Combine this rule with rule of higher priority if its level is not equal
 * If you want to combine with a rule of lower priority, call other->combine(this)
 */
FlowNode* FlowNode::combine(FlowNode* other, bool as_child) {
    assert(as_child == false); //Child is deprecated (use replace_leaves), can only add excluding entries
	if (other == 0) return this;
    other->check();
    this->check();


	/*if (other->level()->get_max_value() > level()->get_max_value()) {
#if DEBUG_CLASSIFIER
		click_chatter("COMBINE : swapping nodes");
#endif
		return other->combine(this);
	}*/

	if (other->parent()) {
#if DEBUG_CLASSIFIER
		click_chatter("COMBINE : swapping parent");
#endif
		assert(parent() == 0);
		set_parent(other->parent());
		other->set_parent(0);
	}

	assert(other->parent() == 0);

	if (level()->equals(other->level())) { //Same level
#if DEBUG_CLASSIFIER
		click_chatter("COMBINE : same level");
#endif

		FlowNode::NodeIterator* other_childs = other->iterator();

		FlowNodePtr* other_child_ptr;
		while ((other_child_ptr = other_childs->next()) != 0) { //For each child of the other node
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
					child_ptr->node = child_ptr->node->combine(other_child_ptr->node, as_child);
					child_ptr->node->set_parent(this);
				}
			}

			//Delete the child of the combined rule
			other_child_ptr->node = 0;
			other->dec_num();
		}
		delete other;
        this->check();
		return this;
	} else if (dynamic_cast<FlowLevelDummy*>(other->level()) != 0) { //Other level is dummy
#if DEBUG_CLASSIFIER
		click_chatter("COMBINE : with a default route");
#endif
		//Merging with a default route
		if (_default.ptr != 0) {
			if (_default.is_leaf() || (other->default_ptr() && other->default_ptr()->is_leaf())) {
				click_chatter("COMBINE : Cannot combine leaf (a is %d, b is %d)!",_default.is_leaf(),other->default_ptr() && other->default_ptr()->is_leaf());
				this->print();
				other->print();
				click_chatter("So, what do i do?");
				assert(false);
			}
			set_default(_default.node->combine(other->_default.node,as_child));
			return this;
		}

		if (!other->_default.ptr) {
#if DEBUG_CLASSIFIER
			click_chatter("There is no default value ...?");
#endif
			return this;
		}

		_default = other->_default;
		_default.set_parent(this);
		other->_default.ptr = 0; //Remove default from other node so it's not freed
		delete other;
        this->check();
		return this;
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

	if (dynamic_cast<FlowLevelDummy*>(this->level()) != 0) {
		//If this is a dummy, we can directly set our leaf as the child
		assert(this->default_ptr()->is_leaf());
		if (other->default_ptr()->ptr == 0) {
            other->check();
            click_chatter("This is a dummy, ignoring myself by setting my leaf to other and returning it");
			other->set_default(this->default_ptr()->leaf);
            other->check();
            return other;
		} else {
            click_chatter("This is a dummy, and other has a default already");
			if (other->default_ptr()->is_node()) {
			    FlowNode* other_default_node = other->default_ptr()->node;
			    other->default_ptr()->ptr = 0;
				other->set_default(other_default_node->combine(this,as_child));
			} else {
				click_chatter("BUG : Combining a leaf with dummy?");
				click_chatter("merging leaf :");
				this->default_ptr()->print();
				click_chatter("from :");
				this->print();
				click_chatter("with :");
				other->print();
				assert(false);
			}
		}
        other->check();
	} else {
        click_chatter("Combining different tables (%s and %s) is not supported for now !",
                level()->print().c_str(),
                other->level()->print().c_str());
        if (default_ptr()->ptr == 0) {
            click_chatter("Adding second as default");
            set_default(other);
            return this;
        } else {
            click_chatter("Adding this as default. WARNING : Priority may change");
            if (other->default_ptr()->ptr == 0) {
                other->set_default(this);
            } else {
                if (other->default_ptr()->is_node())
                    other->set_default(other->default_ptr()->node->combine(this,as_child));
                else {
                    click_chatter("BUG : Combining a leaf?");
                    click_chatter("merging leaf :");
                    this->default_ptr()->print();
                    click_chatter("from :");
                    this->print();
                    click_chatter("with :");
                    other->print();
                    assert(false);
                }
            }
        }
	}
    other->check();
	return other;


}

/**
 * Replace all leave of this node.
 * Leave data is lost, you must re-setup FCB initial values after calling this fct
 *
 */
FlowNode* FlowNode::replace_leaves(FlowNode* other)  {
        if (other == 0) return this;
        auto fnt = [other](FlowNodePtr* ptr) {
            assert(ptr != 0);
            FlowNodeData data = ptr->data();
            FlowNode* parent = ptr->parent();
            ptr->leaf->release(1);
            ptr->leaf = 0;

            FlowNode* no = other->duplicate(true, 1);
            ptr->set_node(no);
            ptr->set_data(data);
            no->_parent = parent;

        };
        this->traverse_ptr(fnt);
        delete other;
        return this;
}

FlowNode* FlowNode::optimize() {
	NodeIterator* it = iterator();
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

			FlowNodePtr* child = (iterator()->next());

			if (_default.ptr == 0) {
				if (child->is_leaf()) {
#if DEBUG_CLASSIFIER
					click_chatter("Optimize : one leaf child and no default value : creating a dummy level !");
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
			delete this;
            newnode->check();
			return newnode;
		} else if (getNum() == 2) {
			FlowNode* newnode;
			NodeIterator* cit = iterator();
			FlowNodePtr* childA = (cit->next());
			FlowNodePtr* childB = (cit->next());
			if (childB->else_drop() || !childA->else_drop()) {
			    FlowNodePtr* childTmp = childB;
			    childB = childA;
			    childA = childTmp;
			}

			if (_default.ptr == 0 && !childB->else_drop()) {
#if DEBUG_CLASSIFIER
				click_chatter("Optimize : 2 child and no default value : only 2 possible case (value %lu or value %lu)!",childA->data().data_64,childB->data().data_64);
#endif
				FlowNodeTwoCase* fl = new FlowNodeTwoCase(childA->optimize());
				fl->inc_num();
				fl->assign(this);
				fl->set_default(childB->optimize());
				newnode = fl;
			} else {
#if DEBUG_CLASSIFIER
				click_chatter("Optimize : only 3 possible cases (value %lu, value %lu or default %lu)",childA->data().data_64,childB->data().data_64,_default.data().data_64);
#endif
				FlowNodePtr ncA = childA->optimize();
				FlowNodePtr ncB = childB->optimize();
				ncA.print();
				ncB.print();
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
			delete this;
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
        return ptr;
    }
}

bool FlowNodePtr::else_drop() {
    if (is_leaf())
        return false;
    else
        return dynamic_cast<FlowNodeDefinition*>(node)->_else_drop;
}

FlowNodePtr* FlowNode::get_first_leaf_ptr() {
	FlowNode* parent = this;
	FlowNodePtr* current_ptr = 0;

	do {
		if (parent->getNum() > 0)
			current_ptr = parent->iterator()->next();
		else
			current_ptr = parent->default_ptr();
	} while(!current_ptr->is_leaf() && (parent = current_ptr->node));

	return current_ptr;

}

#if DEBUG_CLASSIFIER
/**
 * Ensure consistency of the tree
 * @param node
 */
void FlowNode::check() {
    FlowNode* node = this;
    NodeIterator* it = node->iterator();
    FlowNodePtr* cur = 0;
    int num = 0;
    while ((cur = it->next()) != 0) {
        num++;
        if (cur->is_node()) {
            if (cur->node->parent() != node)
                goto error;
            cur->node->check();
        } else {
#if HAVE_DYNAMIC_FLOW_RELEASE_FNT
            assert(cur->leaf->release_pool);
#endif
        }
    }

    if (num != getNum()) {
        click_chatter("Number of child error (live count %d != theorical count %d) in :",num,getNum());
        print();
        assert(num == getNum());
    }

    if (node->default_ptr()->ptr != 0) {
        if (node->default_ptr()->parent() != node)
            goto error;
        if (node->default_ptr()->is_node()) {
            assert(node->level()->is_dynamic() || node->get_default().node->parent() == node);
            node->get_default().node->check();
        } else {
#if HAVE_DYNAMIC_FLOW_RELEASE_FNT
            assert(node->default_ptr()->leaf->release_pool);
#endif
        }
    }
    return;
    error:
    click_chatter("Parent concistancy error in :");
    print();
    assert(false);
}
#endif

void FlowNode::print(FlowNode* node,String prefix) {
	click_chatter("%s%s (%s, %d childs) %p Parent:%p",prefix.c_str(),node->level()->print().c_str(),node->name().c_str(),node->getNum(),node,node->parent());

	NodeIterator* it = node->iterator();
	FlowNodePtr* cur = 0;
	while ((cur = it->next()) != 0) {

		if (!cur->is_leaf()) {
			click_chatter("%s|-> %lu Parent:%p",prefix.c_str(),cur->data().data_64,cur->parent());
			print(cur->node,prefix + "|  ");
		} else {
			cur->leaf->print(prefix + "|->");
		}
	}

	if (node->_default.ptr != 0) {
		if (node->_default.is_node()) {
			click_chatter("%s|-> DEFAULT %p Parent:%p",prefix.c_str(),node->_default.ptr,node->_default.parent());
			assert(node->level()->is_dynamic() || node->_default.node->parent() == node);
			print(node->_default.node,prefix + "|  ");
		} else {
			node->_default.leaf->print(prefix + "|-> DEFAULT");
		}
	}
}

void FlowControlBlock::print(String prefix) {
	char data_str[64];
	int j = 0;

	for (unsigned i = 0; i < get_pool()->data_size() && j < 60;i++) {
		sprintf(&data_str[j],"%02x",data[i]);
		j+=2;
	}
	click_chatter("%s %lu Parent:%p UC:%d (%p data %s)",prefix.c_str(),node_data[0].data_64,parent,count(),this,data_str);
}

void FlowNodePtr::print() {
	if (is_leaf())
		leaf->print("");
	else
		node->print();
}

/**
 * Create best structure for this node, and optimize all childs
 */
FlowNode* FlowNode::create_final() {
    FlowNode * fl;
    //click_chatter("Level max is %u, deletable = %d",level->get_max_value(),level->deletable);
    if (_level->get_max_value() == 0)
        fl = new FlowNodeDummy();
    else if (_level->get_max_value() > 256)
        fl = new FlowNodeHash();
    else
        fl = new FlowNodeArray(_level->get_max_value());
    fl->_level = _level;
    fl->_child_deletable = _level->is_deletable();
    fl->_parent = parent();

    NodeIterator* it = iterator();
    FlowNodePtr* cur = 0;
    while ((cur = it->next()) != 0) {
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

int NR_SHARED_FLOW = 0;

#endif

CLICK_ENDDECLS
