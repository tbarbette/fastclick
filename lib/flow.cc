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

CLICK_DECLS

#ifdef HAVE_FLOW


__thread FlowControlBlock* fcb_stack = 0;

const int FlowNodeHash::HASH_SIZES_NR = 10;
//const int FlowNodeHash::hash_sizes[FlowNodeHash::HASH_SIZES_NR] = {257,521,1031,2053,4099,8209,16411,32771,65539,131072}; //Prime for less collisions, after the last we double
const int FlowNodeHash::hash_sizes[FlowNodeHash::HASH_SIZES_NR] = {256,512,1024,2048,4096,8192};

void FlowClassificationTable::set_release_fnt(SubFlowRealeaseFnt pool_release_fnt) {
	_pool_release_fnt = pool_release_fnt;
}


FlowNode* FlowNode::combine(FlowNode* other) {
	if (other == 0) return this;

	if (other->level()->get_max_value() > level()->get_max_value()) {
#if DEBUG_CLASSIFIER
		click_chatter("COMBINE : swapping nodes");
#endif
		return other->combine(this);
	}

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
		while ((other_child_ptr = other_childs->next()) != 0) {
#if DEBUG_CLASSIFIER
			click_chatter("COMBINE : taking child %lu",other_child_ptr->data().data_64);
#endif
			FlowNodePtr* child_ptr = find(other_child_ptr->data());
			if (child_ptr->ptr == 0) {
				*child_ptr = *other_child_ptr;
				child_ptr->set_parent(this);
				inc_num();
			} else {
				if (child_ptr->is_leaf() || other_child_ptr->is_leaf()) {
#if DEBUG_CLASSIFIER
					click_chatter("Combining leaf???");
#endif
				} else {
					child_ptr->node = child_ptr->node->combine(other_child_ptr->node);
					child_ptr->node->set_parent(this);
				}
			}

			//Delete the child of the combined rule
			other_child_ptr->node = 0;
			other->dec_num();
		}
		delete other;
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
			set_default(_default.node->combine(other->_default.node));
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
		return this;
	} else if (dynamic_cast<FlowLevelOffset*>(level()) && dynamic_cast<FlowLevelOffset*>(other->level())) {
#if DEBUG_CLASSIFIER
		click_chatter("COMBINE : two level offset");
#endif
		FlowLevelOffset* o1 = dynamic_cast<FlowLevelOffset*>(level());
		FlowLevelOffset* o2 = dynamic_cast<FlowLevelOffset*>(other->level());
		/*if (o1->get_max_value() == o2->get_max_value() && abs(o1->offset() - o2->offset())) { //Offset is different

		}*/
	}

	click_chatter("Combining different tables (%s and %s) is not supported for now, the second one will be added as default !",level()->print().c_str(),other->level()->print().c_str());
	if (this->default_ptr()->ptr == 0) {
		this->set_default(other);
	} else {
		assert(this->default_ptr()->is_node());
		this->set_default(this->default_ptr()->node->combine(other));
	}
	return this;


}
FlowNode* FlowNode::optimize() {
	NodeIterator* it = iterator();
	FlowNodePtr* ptr;

	//Optimize default
	if (_default.ptr && _default.is_node())
		_default.node->optimize();

	//Optimize all childs
	while ((ptr = it->next()) != 0) {
		if (ptr->is_node()) {
			FlowNodeData data = ptr->data();
			ptr->node = ptr->node->optimize();
			ptr->node->set_parent(this);
			ptr->set_data(data);
		}
	}

	if (!level()->is_dynamic()) {
		if (getNum() == 0) {
			//No nead for this level
			if (default_ptr()->is_leaf()) {
#if DEBUG_CLASSIFIER
				click_chatter("Optimize : no need for this level");
#endif
				return default_ptr()->node;
			} else {
				if (dynamic_cast<FlowNodeDummy*>(this) == 0) {
					FlowNodeDummy* fl = new FlowNodeDummy();
					fl->assign(this);
					fl->_default = _default;
					_default.ptr = 0;
					delete this;
					return fl;
				}
			}
		} else if (getNum() == 1) {
			FlowNode* newnode;

			FlowNodePtr* child = (iterator()->next());
			if (_default.ptr == 0) {
				if (child->is_leaf()) {
#if DEBUG_CLASSIFIER
					click_chatter("Optimize : one child and no default value : creating a dummy level !");
#endif
					FlowNodeDummy* fl = new FlowNodeDummy();
					fl->assign(this);
					fl->_default = *child;
					newnode = fl;
				} else {
#if DEBUG_CLASSIFIER
					click_chatter("Optimize : one child and no default value : no need for this level !");
#endif
					newnode = child->node;
				}
			} else {
#if DEBUG_CLASSIFIER
				click_chatter("Optimize : only 2 possible case (value %lu or default %lu)",child->data().data_64,_default.data().data_64);
#endif
				FlowNodeTwoCase* fl = new FlowNodeTwoCase(*child);
				fl->assign(this);
				fl->inc_num();
				fl->_default = _default;
				newnode = fl;
				_default.ptr = 0;
			}
			child->set_parent(newnode);
			child->ptr = 0;
			dec_num();
			delete this;
			return newnode;
		} else if (getNum() == 2) {
			FlowNode* newnode;
			NodeIterator* cit = iterator();
			FlowNodePtr* childA = (cit->next());
			FlowNodePtr* childB = (cit->next());
			if (_default.ptr == 0) {
#if DEBUG_CLASSIFIER
				click_chatter("Optimize : 2 child and no default value : only 2 possible case (value %lu or value %lu)!",childA->data().data_64,childB->data().data_64);
#endif
				FlowNodeTwoCase* fl = new FlowNodeTwoCase(*childA);
				fl->inc_num();
				fl->assign(this);
				fl->_default = *childB;
				newnode = fl;
			} else {
#if DEBUG_CLASSIFIER
				click_chatter("Optimize : only 3 possible case (value %lu, value %lu or default %lu)",childA->data().data_64,childB->data().data_64,_default.data().data_64);
#endif
				FlowNodeThreeCase* fl = new FlowNodeThreeCase(*childA,*childB);
				fl->inc_num();
				fl->inc_num();
				fl->assign(this);
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
			return newnode;
		} else {
#if DEBUG_CLASSIFIER
			click_chatter("No optimization for level with %d childs",getNum());
#endif
			//TODO kind of array-search if small
			return this;
		}
	} else {
#if DEBUG_CLASSIFIER
		click_chatter("Dynamic level won't be optimized");
#endif
	}

	return this;
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

void FlowNode::print(FlowNode* node,String prefix) {
	click_chatter("%s%s (%s, %d childs) %p Parent:%p",prefix.c_str(),node->level()->print().c_str(),node->name().c_str(),node->getNum(),node,node->parent());

	NodeIterator* it = node->iterator();
	FlowNodePtr* cur = 0;
	while ((cur = it->next()) != 0) {

		if (!cur->is_leaf()) {
			click_chatter("%s|-> %lu Parent:%p",prefix.c_str(),cur->data().data_64,cur->parent());
			assert(cur->node->parent() == node);
			print(cur->node,prefix + "|  ");
		} else {
			char data_str[64];
			int j = 0;
			for (int i = 0; i < cur->leaf->release_pool->data_size() && j < 60;i++) {
				sprintf(&data_str[j],"%02x",cur->leaf->data[i]);
				j+=2;
			}
			click_chatter("%s|-> %lu Parent:%p (data %s)",prefix.c_str(),cur->data().data_64,cur->parent(),data_str);
			//click_chatter("%s|    Data %d %d",prefix.c_str(),cur->leaf->data_32[2],cur->leaf->data_32[3]);
		}
	}

	if (node->_default.ptr != 0) {
		click_chatter("%s|-> DEFAULT Parent:%p",prefix.c_str(),node->_default.parent());
		if (node->_default.is_node()) {
			assert(node->level()->is_dynamic() || node->_default.node->parent() == node);
			print(node->_default.node,prefix + "|  ");
		}
	}
}

int NR_SHARED_FLOW = 0;

#endif

CLICK_ENDDECLS
