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
	if (one_upstream_classifier() == 0) {
		return errh->error("%s : Please place a FlowClassifier element before any FlowDispatcher",name().c_str());
	}
	int defaultOutput = 0;

	rules.resize(conf.size());
	for (int i = 0; i < conf.size(); i++) {
	    String s = String(conf[i]);

		if (_verbose)
			click_chatter("Line : %s",s.c_str());

		rules[i] = FlowClassificationTable::parse(s, _verbose);
		if (rules[i].output == INT_MAX) {
		    rules[i].output = defaultOutput++;
		    if (rules[i].output >= noutputs()) {
		        if (rules[i].is_default)
		            rules[i].output = -1;
		        else
		            rules[i].output = noutputs() - 1;
		    }
		} else if (rules[i].output < 0) {
		    rules[i].output = -1;
		} else {
		    if (rules[i].output >= defaultOutput)
		        defaultOutput = rules[i].output + 1;
		}
		if (rules[i].root == 0) {
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
	if (rules.size() == 0 || !rules[rules.size() -1].is_default) {
	    rules.push_back(FlowClassificationTable::make_drop_rule());
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
				rules[i].root->print(_flow_data_offset);
			}
		}
		FlowNode* merged = 0;
#if DEBUG_CLASSIFIER
		//Copy the rules to check that all path indeed lead to the correct output afterwards
		Vector<FlowClassificationTable::Rule> rules_copy = rules;
		for (int i = 0; i < rules.size() ; ++i) {
		    rules_copy[i].root = rules[i].root->duplicate(true, 1);
		}
#endif

		for (int i = 0; i < rules.size() ; ++i) {
			//First merge the table after the output to the final node of this rule
		    rules[i].root->check();

#if DEBUG_CLASSIFIER
            click_chatter("%p{element} : Writing output number %d to rules %d", this, rules[i].output, i);
#endif
			//Now set data for all leaf of the rule
            auto fnt = [this,i](FlowNodePtr* ptr) {
                *((uint32_t*)(&ptr->leaf->data[_flow_data_offset])) = (uint32_t)rules[i].output;
                if (rules[i].output < 0 || rules[i].output >= noutputs()) { //If it's a real output, merge with children
                    ptr->leaf->set_early_drop();
                }
            };
			rules[i].root->traverse_all_leaves(fnt, true, true);

			if (_verbose)
				click_chatter("Merging rule %d of %p{element} (output %d)",i,this,rules[i].output);

			if (merged == 0) {
				merged = rules[i].root;
			} else {
			    //We must replace all default path per the new rule
				merged = merged->combine(rules[i].root, false, i > 0 && rules[i - 1].output != rules[i].output);
			}
			merged->check();

#if DEBUG_CLASSIFIER
	/*		click_chatter("Result of merging rule to the graph : ",i,this);
			merged->print(_flow_data_offset);
			if (rules[i].is_default) {
			    uint32_t o = rules[i].output;
			    //Assert all default are defined
			    auto check_fnt = [o,this](FlowNode* ptr) -> bool {
                    assert(ptr->default_ptr()->ptr);
                    assert(ptr->default_ptr()->is_leaf());
                    //assert(*((uint32_t*)(&ptr->default_ptr()->leaf->data[_flow_data_offset])) == o); //This is not true, some default may be sub-case of one of defined rules
                    return true;
                };
			    merged->traverse_all_default_leaf(check_fnt);

			}
    #if DEBUG_CLASSIFIER
            //This is exponential
            //Check that there is no multiple classification on the same node
            merged->traverse_all_nodes([merged](FlowNode* parent) -> bool {
                parent->traverse_all_nodes([parent](FlowNode* node) -> bool {
                    assert(!parent->level()->equals(node->level()));
                    return true;
                });
                return true;
            });
    #endif*/
#endif
		}
		assert(merged);

        if (_verbose) {
            click_chatter("Table for %s before merging children :",name().c_str());
            merged->print(_flow_data_offset);
        }
        auto fnt = [this](FlowNodePtr* ptr) {
            int output = *((uint32_t*)(&ptr->leaf->data[_flow_data_offset]));
            if (output >= 0 && output < noutputs()) { //If it's a real output, merge with children
                   FlowNode* child_table = FlowElementVisitor::get_downward_table(this, output);
                   if (child_table) {
                       child_table->check();
                       if (!child_table->has_no_default()) {
                          if (_verbose > 1)
                              click_chatter("Child has default values : appending drop rule");
                           child_table = child_table->combine(FlowClassificationTable::parse("- drop").root, false, true);
                       }
                       //Replace_leaf_with_node takes care of prunning using parent to go up to merged, ensuring no classification on the same field is done twice
                       ptr->replace_leaf_with_node(child_table);
                   }
               }
        };
        merged->traverse_all_leaves(fnt, true, true);
        merged->check();

		if (_verbose) {
			click_chatter("Table for %s after merging children:",name().c_str());
			merged->print(_flow_data_offset);
		}
#if DEBUG_CLASSIFIER
		//This is exponential
        //Check that there is no multiple classification on the same node
        merged->traverse_all_nodes([merged](FlowNode* parent) -> bool {
            parent->traverse_all_nodes([parent](FlowNode* node) -> bool {
                assert(!parent->level()->equals(node->level()));
                return true;
            });
            return true;
        });
#endif
		_table = merged;
        _table->check();
#if DEBUG_CLASSIFIER
        /**
         * Verification that all rule leads to the right output
         */
        //Anyt is used to check for the else rule
        FlowNodePtr anynt = FlowNodePtr(_table->duplicate(true, 1));
        for (int i = 0; i < rules_copy.size() ; ++i) {
            click_chatter("VERIF Rule (id default %d):",rules_copy[i].is_default);
            rules_copy[i].root->traverse_all_leaves([this,rules_copy,i,&anynt](FlowNodePtr* node){
                if (anynt.is_leaf()) {
                    if (!anynt.leaf->data[_flow_data_offset] == rules_copy[i].output) {
                        click_chatter("VERIF Final leaf does not lead to output %d :",rules_copy[i].output);
                        anynt.print();
                        click_chatter("VERIF Rule (id default %d):",rules_copy[i].is_default);
                        rules_copy[i].root->print();
                        assert(false);
                    } else {
                        click_chatter("VERIF Rule is ok !");
                        return;
                    }
                }
                FlowNodePtr nt = FlowNodePtr(anynt.node->duplicate(true, 1)); //We check in the tree excluded from the previous rule
                //Remove all data up from the child
                FlowNodeData child_data = node->data();
                node->parent()->traverse_parents([&nt,&child_data,&anynt](FlowNode* parent) {
                    click_chatter("VERIF Pruning %s %lu",parent->level()->print().c_str(),child_data);
                    if (nt.is_node())
                        nt = nt.node->prune(parent->level(), child_data);
                    if (anynt.is_node())
                        anynt = anynt.node->prune(parent->level(), child_data,true);
                    child_data = parent->node_data;
                });


                (rules_copy[i].is_default?anynt:nt).traverse_all_leaves([this,rules_copy,i,nt,anynt](FlowNodePtr* cur){
                    if (!cur->leaf->data[_flow_data_offset] == rules_copy[i].output) {
                        click_chatter("VERIF Leaf does not lead to output %d :",rules_copy[i].output);
                        cur->leaf->print("",_flow_data_offset);
                        click_chatter("VERIF Pruned tree : ");
                        (rules_copy[i].is_default?anynt:nt).print();
                        click_chatter("VERIF Rule (id default %d):",rules_copy[i].is_default);
                        rules_copy[i].root->print(_flow_data_offset);
                        assert(false);
                    } else {
                        click_chatter("VERIF Rule is ok !");
                        return;
                    }
                });
            },true,true);
        }
#endif
	} else {
        _table->check();
    }
	//click_chatter("Table before duplicate : ");

	FlowNode* tmp = _table->duplicate(true,1);
	tmp->check();
	assert(tmp->has_no_default());
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
