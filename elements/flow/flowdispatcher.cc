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
	if (upstream_classifier() == 0) {
		return errh->error("%s : Please place a FlowClassifier element before any FlowDispatcher",name().c_str());
	}
	int defaultOutput = 0;

	rules.resize(conf.size());
	for (int i = 0; i < conf.size(); i++) {
	    String s = String(conf[i]);

		if (_verbose)
			click_chatter("Line : %s",s.c_str());

		rules[i] = upstream_classifier()->table().parse(s, _verbose);
		if (rules[i].output == INT_MAX)
		    rules[i].output = defaultOutput++;
		if (rules[i].root != 0) {
		    int outputNr = rules[i].output;
		    auto fnt = [this,outputNr](FlowControlBlock* leaf){
		        leaf->data[_flow_data_offset] = outputNr;
		    };
		    rules[i].root->traverse(fnt);


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




FlowNode* FlowDispatcher::get_table(int) {
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
					if (dynamic_cast<FlowLevelDummy*>(rules[i].root->level()) != 0 && rules[i].root->get_default().is_leaf()) {
					    delete rules[i].root;
					    rules[i].root = child_table;
					} else {
                        rules[i].root->replace_leaves(child_table);
					}
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
				merged = merged->combine(rules[i].root,false);
			}
		}
		assert(merged);
		//Insert a "- drop" rule
		if (merged->default_ptr()->ptr == 0) {
			click_chatter("ADDING ! %s to %s",merged->name().c_str(),merged->level()->print().c_str());
			FlowNodePtr* parent_ptr = merged->default_ptr();
			parent_ptr->set_leaf(upstream_classifier()->table().get_pool()->allocate());
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
