/*
 * flowdispatcher.{cc,hh}
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/flow.hh>
#include "flowdispatcher.hh"
#include <iostream>
#include <string>
#include <sstream>
#include <algorithm>
#include <vector>
#include <iterator>
#include <regex>

CLICK_DECLS

FlowDispatcher::FlowDispatcher() : _table(0)  {
#if DEBUG_CLASSIFIER
	_verbose = true;
#else
	_verbose = false;
#endif

};

int
FlowDispatcher::configure(Vector<String> &conf, ErrorHandler *errh)
{
	bool subflow = false;
	bool thread = false;
	int defaultOutput = 0;

	if (upstream_classifier() == 0) {
		return errh->error("%s : Please place a FlowClassifier element before any FlowDispatcher",name().c_str());
	}

	std::regex reg("((agg )?((?:(?:ip)?[+]?[0-9]+/[0-9a-fA-F]+?/?[0-9a-fA-F]+? ?)+)|-)( keep| [0-9]+| drop)?",
			 std::regex_constants::icase);
	std::regex classreg("(ip)?[+]?([0-9]+)/([0-9a-fA-F]+)?/?([0-9a-fA-F]+)?",
				 std::regex_constants::icase);

	for (int i = 0; i < conf.size(); i++) {
		FlowNode* root = 0;
		FlowNodePtr* parent_ptr = 0;
		bool deletable_value = false;
		int output = 0;



		std::smatch result;
		std::string s = std::string(conf[i].c_str());
		if (_verbose)
			click_chatter("Line : %s",s.c_str());
		if (conf[i] == "thread") {
			thread = true;
			/*		} else if (conf[i] == "subflow") {
						subflow = true;*/
		} else if (std::regex_match(s, result, reg)){
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
				output = defaultOutput++;
			}

			FlowNode* parent = 0;
			if (other != "-") {
				std::string classs = result.str(3);

				std::regex_iterator<std::string::iterator> it (classs.begin(), classs.end(), classreg);
				std::regex_iterator<std::string::iterator> end;

				while (it != end)
				{
					if (_verbose)
						click_chatter("Class : %s",it->str(0).c_str());
					std::string layer = it->str(1);
					std::string offset = it->str(2);
					std::string value = it->str(3);
					std::string mask = it->str(4);

					if (_verbose)
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
					if (_verbose)
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
						if (_verbose)
							click_chatter("AGG Offset : %d, mask : 0x%lx",fl->offset,fl->mask);
						f = fl;
					} else if (maskv <= UINT8_MAX){
						FlowLevelGeneric8* fl = new FlowLevelGeneric8();
						fl->set_match(std::stoul(offset),maskv);
						if (_verbose)
							click_chatter("HASH8 Offset : %d, mask : 0x%lx",fl->offset(),fl->mask());
						f = fl;
					} else if (maskv <= UINT16_MAX){
						FlowLevelGeneric16* fl = new FlowLevelGeneric16();
						fl->set_match(std::stoul(offset),maskv);
						if (_verbose)
							click_chatter("HASH16 Offset : %d, mask : 0x%lx",fl->offset(),fl->mask());
						f = fl;
					} else if (maskv <= UINT32_MAX){
						FlowLevelGeneric32* fl = new FlowLevelGeneric32();
						fl->set_match(std::stoul(offset),maskv);
						if (_verbose)
							click_chatter("HASH32 Offset : %d, mask : 0x%lx",fl->offset(),fl->mask());
						f = fl;
					} else {
						FlowLevelGeneric64* fl = new FlowLevelGeneric64();
						fl->set_match(std::stoul(offset),maskv);
						if (_verbose)
							click_chatter("HASH64 Offset : %d, mask : 0x%lx",fl->offset(),fl->mask());
						f = fl;
					}

					FlowNode* node = FlowNode::create(parent,f);

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
				if (_verbose)
					click_chatter("Class : -");
				FlowLevel*  f = new FlowLevelDummy();

				root = FlowNode::create(root,f);
				parent = root;
				//click_chatter("Default node to output %d",output);
				parent_ptr = root->default_ptr();
			}

			parent_ptr->set_leaf(upstream_classifier()->table().get_pool().allocate());
			parent_ptr->set_data(lastvalue);
			parent_ptr->leaf->parent = parent;
			parent_ptr->leaf->acquire(1);
			parent_ptr->leaf->data[_flow_data_offset] = output;

			root->check();
			rules.push_back((Rule){.root = root, .output = output});
		} else {
			return errh->error("argument %d is not a valid defined subflow (%s)", i+1,s.c_str());
		}
		if (_verbose)
			click_chatter("Rule %d to output %d",rules.size(),rules[rules.size() - 1].output);
		//rules[rules.size() - 1].root->print();
		/*using namespace std;
					istringstream iss(conf[i]);
					vector<string> tokens{istream_iterator<string>{iss},
					         istream_iterator<string>{}};

					vector<FlowLevel> network;

					int j = 0;

						string word = tokens[j];
						if (word == "type") {
							type = true;
						} else if (word == "src") {
							j++;
							word = tokens[j];
							if (word == "port") {
								srcport = true;
							} else if (word == "host") {
								srchost = true;
							} else {
								errh->error("\"%s\" is not a valid defined subflow",conf[i]);
								return -EINVAL;
							}
						} else if (word == "dst") {
							j++;
							word = tokens[j];
							if (word == "port") {
								dstport = true;
							} else if (word == "host") {
								dsthost = true;
							} else {
								errh->error("\"%s\" is not a valid defined subflow",conf[i]);
								return -EINVAL;
							}
						} else if (word == "thread") {
							_thread = true;
						} else if (word == "subflow") {
							subflow = true;
						} else if (word == "assume") {
							j++;
							word = tokens[j];
							if (word == "proto") {
								proto_known = true;
								j++;
								word = tokens[j];
								proto = word;

							} else if (word == "ip4") {
								ip = true;
								check_ip_v = false;
								ip_v_set = true;
							}
						} else {
							errh->error("keyword '%s' in argument %d is unknown", word , i+1);
								return -EINVAL;
						}

						if (proto == "tcp" || proto == "udp")
							ip = true;

						if (j < tokens.size() - 1) {
							errh->error("argument %d is not a valid defined subflow (%s)", i+1,conf[i]);
								return -EINVAL;
						}*/

	}


	/*if (subflow) {
					FlowLevelSubflow* sbf = new FlowLevelSubflow();
					sbf->deletable = false;
					flow_levels.push_back(sbf);
					subflow_level = 1;
				} else {
					subflow_level = 0;
				}
	 */
	/*if (thread) {
		FlowLevelThread* sbt = new FlowLevelThread(master()->nthreads());
		sbt->deletable = false;

	}*/

	return 0;
}




FlowNode* FlowDispatcher::get_table() {
	if (!_table) {
		if (_verbose) {
			click_chatter("%s : Computing table with %d rules :",name().c_str(),rules.size());
			for (int i = 0; i < rules.size(); ++i) {
			    rules[i].root->check();
				rules[i].root->print();
			}
		}
		FlowNode* merged = 0;

		for (int i = rules.size() - 1; i >= 0 ; --i) {
			//First merge the table after the output to the final node of this rule
		    rules[i].root->check();
			if (_verbose)
				click_chatter("Merging this rule of %s :",name().c_str());
			rules[i].root->print();
			if (rules[i].output > -1 && rules[i].output < noutputs()) {
				FlowNode* child_table = FlowElementVisitor::get_downward_table(this, rules[i].output);
				if (child_table) {
					child_table->check();
					FlowNodePtr* leaf_ptr = rules[i].root->get_first_leaf_ptr();
					FlowControlBlock* leaf = leaf_ptr->leaf;
					FlowNode* parent = leaf_ptr->parent();
					assert(parent);
#if DEBUG_CLASSIFIER
					//click_chatter("Leaf data %lu is now replaced with child table :",current_ptr->data().data_64);
#endif
					leaf_ptr->set_node(child_table);
					leaf_ptr->set_parent(parent);
					leaf_ptr->set_data(leaf->node_data[0]);
					leaf->release(1);
					if (_verbose) {
						click_chatter("Print rule merged with child of %p :",rules[i].root);
						rules[i].root->print();
					}
				}
			}

			rules[i].root->check();


			//Now set data for all leaf of the rule (now appended with all leafs of the child)
			FlowNode::LeafIterator* it = rules[i].root->leaf_iterator();
			FlowControlBlock* leaf;
			while ((leaf = it->next()) != 0) {
				*((uint32_t*)((uint8_t*)&leaf->data[_flow_data_offset])) = rules[i].output;
			}

			if (_verbose)
				click_chatter("Merging rule %d of %p{element}",i,this);

			if (merged == 0) {
				merged = rules[i].root;
			} else {
				merged = merged->combine(rules[i].root);
			}
		}
		assert(merged);
		//Insert a "- drop" rule
		if (merged->default_ptr()->ptr == 0) {
			click_chatter("ADDING ! %s to %s",merged->name().c_str(),merged->level()->print().c_str());
			FlowNodePtr* parent_ptr = merged->default_ptr();
			parent_ptr->set_leaf(upstream_classifier()->table().get_pool().allocate());
			parent_ptr->leaf->parent = merged;
			parent_ptr->leaf->acquire(1);
			parent_ptr->leaf->data[_flow_data_offset] = -1;
		}
		merged->check();
		merged->print();
		if (_verbose) {
			click_chatter("Table for %s :",name().c_str());
			merged->print();
		}
		_table = merged;
        _table->print();
        _table->check();
	} else {
        _table->print();
        _table->check();
    }
	//click_chatter("Table before duplicate : ");

	FlowNode* tmp = _table->duplicate(true,1);
	//click_chatter("Table after duplicate :");
	tmp->print();
	tmp->check();
	return tmp;
}


int FlowDispatcher::initialize(ErrorHandler *errh) {
	//delete _table;
	return 0;
}




void FlowDispatcher::push_batch(int, int* flowdata, PacketBatch* batch) {
	//click_chatter("%s : %d packets to output %d",name().c_str(),batch->count(),*flowdata);
	checked_output_push_batch(*flowdata, batch);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(FlowDispatcher)
