/*
 * ctxdispatcher.{cc,hh}
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include "ctxdispatcher.hh"
#include <iostream>
#include <string>
#include <sstream>
#include <algorithm>
#include <vector>
#include <iterator>
#include <click/flow/flow.hh>


CLICK_DECLS

CTXDispatcher::CTXDispatcher() : _table(0)  {
#if DEBUG_CLASSIFIER
	_verbose = true;
#else
	_verbose = false;
#endif
	_children_merge = false;

};

int
CTXDispatcher::configure(Vector<String> &conf, ErrorHandler *errh)
{
    //We need all routes to be parsed before ctx_builded fires, but after the flow space has been initialized
    Router::FctChildFuture* fct = new Router::FctChildFuture([this,conf](ErrorHandler *errh) {
        int defaultOutput = -1;
        rules.resize(conf.size());

        for (int i = 0; i < conf.size(); i++) {
            String s = String(conf[i]);

            if (_verbose) {
                click_chatter("Line : %s",s.c_str());
            }

            rules[i] = FlowClassificationTable::parse(this, s, _verbose, true);
            if (rules[i].output == INT_MAX) { // No output is given
                rules[i].output = ++defaultOutput; //-> take the last seen output + 1
                if (rules[i].output >= noutputs()) {
                    if (rules[i].is_default)
                        rules[i].output = -1;
                    else
                        rules[i].output = noutputs() - 1;
                }
            } else { //The output was set
                if (rules[i].output < 0) { //If it's negative (drop), set it to -1
                    rules[i].output = -1;
                } else { //If it's valid, set the default output to the value set. Ie if the user defines value for port 1, the next unset port should be 2
                    defaultOutput = max(rules[i].output, defaultOutput);
                }
            }
            if (rules[i].root == 0) {
                return errh->error("argument %d is not a valid defined subflow (%s)", i+1,s.c_str());
            }
            if (_verbose)
                click_chatter("Rule %d/%d to output %d",i,rules.size(),rules[i].output);
        }
        if (rules.size() == 0) {
            if (noutputs() == 1)
                rules.push_back(FlowClassificationTable::parse(this, "- 0", _verbose, true));
            else
                return errh->error("Invalid rule set. There is no rule and not a single output.");
        } else if (!rules[rules.size() -1].is_default || _children_merge) {
            if (!_children_merge) {
                bool match_all = true;
                for (int i = 0; i < rules.size(); i++) {
                    if (rules[i].root->level()->is_dynamic() || dynamic_cast<FlowNodeDummy*>(rules[i].root->level())) {

                    } else {
                        match_all = false;
                        break;
                    }
                }
                if (!match_all)
                    click_chatter("%p{element} has no default rule. Non-matching traffic will be dropped. Add a list rule '-' to avoid this message.", this);

            }
            auto r = FlowClassificationTable::make_drop_rule();
            if (defaultOutput < noutputs() - 1) { //The last output is a reject port
                r.output = noutputs() - 1;
            }
            rules.push_back(r);
        }
        return 0;
    }, this, CTXManager::ctx_builded_init_future());   //When this context is built, we can notify ctx_builded, which itself will fire only when all children called


    //Only when the FCB is built in all managers (i.e. fcb_builded_init_future fires), we can start writing into FCBs
    VirtualFlowManager::fcb_builded_init_future()->post(fct);

	return 0;
}

FlowNode* CTXDispatcher::get_child(int output, bool append_drop,Vector<FlowElement*> context) {
    FlowNode* child_table = FlowElementVisitor::get_downward_table(this, output, context, _children_merge);
    if (!child_table)
        return 0;
    child_table->check();
    if (!child_table->has_no_default() && append_drop) {
        if (_verbose)
           click_chatter("Child has default values : appending drop rule");
        child_table = child_table->combine(FlowClassificationTable::parse(this, "- drop").root, false, true, true, this);
    }
    return child_table;
}

bool CTXDispatcher::attach_children(FlowNodePtr* ptr, int output, bool append_drop, Vector<FlowElement*> context) {
    FlowNode* child_table = get_child(output, append_drop, context);
    bool changed = false;
    if (child_table) {
        changed = ptr->replace_leaf_with_node(child_table, true, this);
        //Replace_leaf_with_node takes care of prunning using parent to go up to merged, ensuring no classification on the same field is done twice
    }
    if (ptr->is_node()) {
        auto fnt = [this,output](FlowNodePtr* ptr) {
            fcb_set_init_data(ptr->leaf, output);
        };
        ptr->node->traverse_all_leaves(fnt, true, true);
    } else {
        fcb_set_init_data(ptr->leaf, output);
    }
    return changed;
}


FlowNode* CTXDispatcher::get_table(int, Vector<FlowElement*> context) {
	if (!_table) {
        //Set the output leafs
/*        Vector<FlowControlBlock*> output_fcb;
        output_fcb.resize(noutputs() + 1);
        for (int i = 0; i <= noutputs(); i++) {
            FlowControlBlock* fcb = FCBPool::init_allocate();
            fcb->acquire(1);
            output_fcb[i] = fcb;
            fcb->parent = 0;
            fcb_set_init_data(fcb, i);
        }
        output_fcb[noutputs()]->set_early_drop();
		for (int i = 0; i < rules.size() ; ++i) {
            auto fnt = [this,i,output_fcb](FlowNodePtr* ptr) {
                assert(ptr->ptr == (FlowControlBlock*) -1);
                if (rules[i].output < 0 || rules[i].output >= noutputs()) { //If it's not a real output, set drop flag
                    ptr->set_leaf(output_fcb[noutputs()]);
                }
                else
                    ptr->set_leaf(output_fcb[rules[i].output]);
                assert(ptr->parent() == 0);
            };
            rules[i].root->traverse_all_leaves(fnt, true, true);
        }
*/
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
		    rules_copy[i].root = rules[i].root->duplicate(true, 1, true);
		}
#endif

		for (int i = 0; i < rules.size() ; ++i) {
			//First merge the table after the output to the final node of this rule
		    rules[i].root->check();

#if DEBUG_CLASSIFIER
            click_chatter("%p{element} : Merging output %d with child table", this, rules[i].output);
#endif
            if (_children_merge && (rules[i].output >= 0 && rules[i].output < noutputs())) {
                FlowNode* child_table = get_child(rules[i].output,false,context);
                bool changed = false;
                if (child_table) {
                    child_table->check();
                    rules[i].root = rules[i].root->combine(child_table, true, true, true, this);
                } else {
                    debug_flow("No child table");
                    //Just keep the root
                }
            } else {
                if (_verbose)
                    click_chatter("Not merging children or output %d is not in range", rules[i].output);
            }

            assert(rules[i].root);

#if DEBUG_CLASSIFIER
            click_chatter("%p{element} : Writing output number %d to rules %d", this, rules[i].output, i);
#endif
            //Now set data for all leaf of the rule
            auto fnt = [this,i](FlowNodePtr* ptr) {
                fcb_set_init_data(ptr->leaf, rules[i].output);
                if (rules[i].output < 0 || rules[i].output >= noutputs()) { //If it's not a real output, set drop flag
                    ptr->leaf->set_early_drop();
                }
            };
            rules[i].root->traverse_all_leaves(fnt, true, true);

            if (rules[i].root->is_dummy() && rules[i].root->default_ptr()->is_node()) {
                FlowNode* tmp = rules[i].root;
                rules[i].root = rules[i].root->default_ptr()->node;
                rules[i].root->set_parent(0);
                tmp->default_ptr()->ptr = 0;
                delete tmp;
            }



			if (_verbose) {
				click_chatter("Merging rule %d of %p{element} (output %d)",i,this,rules[i].output);
                click_chatter("---BEGIN---");
				int output = rules[i].output;
	            auto fnt = [this,output](FlowNodePtr* ptr) {
	                assert(*((uint32_t*)(&ptr->leaf->data[_flow_data_offset])) == (uint32_t)output);
	            };
	            rules[i].root->traverse_all_leaves(fnt, true, true);
				rules[i].root->print(_flow_data_offset);
				if (merged)
				    merged->print(_flow_data_offset);
                click_chatter("---END---");
			}

			if (merged == 0) {
				merged = rules[i].root;
			} else {
			    //We must replace all default path per the new rule
				merged = merged->combine(rules[i].root, false, !_children_merge && (i > 0 && rules[i - 1].output != rules[i].output), true, this);
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

        flow_assert(merged->is_full_dummy() || !merged->is_dummy());
        if (!_children_merge) {
            if (merged->is_full_dummy()) {
                int output = *((uint32_t*)(&merged->default_ptr()->leaf->data[_flow_data_offset]));
                FlowNode* child_table = get_child(output,true,context);

                if (child_table) {
                    click_chatter("Children combine");
                    merged = merged->combine(child_table, true, false, true, this);
                }

                auto fnt = [this,output](FlowNodePtr* ptr) {
                    if (ptr->is_node()) {
                        assert(false);
                    } else {
                        fcb_set_init_data(ptr->leaf, output);
                    }
                };
                merged->traverse_all_leaves(fnt, true, true);
            } else {
                auto fnt = [this,context](FlowNodePtr* ptr) {
                    int output = *((uint32_t*)(&ptr->leaf->data[_flow_data_offset]));
                    if (output >= 0 && output < noutputs()) { //If it's a real output, merge with children
                        attach_children(ptr,output,true,context);
                    }
                };
                merged->traverse_all_leaves(fnt, true, true);
                flow_assert(merged->has_no_default());
            }
        };
        merged->check();
        flow_assert(merged->is_full_dummy() || !merged->is_dummy());
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
        /*if (!_children_merge) {
            FlowNodePtr anynt = FlowNodePtr(_table->duplicate(true, 1));
            click_chatter("Anynt before all rules :");
            anynt.print();
            for (int i = 0; i < rules_copy.size() ; ++i) {
                click_chatter("VERIF Rule (id %d default %d):",i,rules_copy[i].is_default);
                rules_copy[i].root->traverse_all_leaves([this,rules_copy,i,&anynt](FlowNodePtr* node){
                    if (anynt.is_leaf()) {
                        if (!anynt.leaf->data[_flow_data_offset] == rules_copy[i].output) {
                            click_chatter("VERIF Final leaf does not lead to output %d :",rules_copy[i].output);
                            anynt.print();
                            click_chatter("VERIF This is a leaf of rule (id default %d):",rules_copy[i].is_default);
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
                    click_chatter("NT before:");
                    nt.print();

                    node->parent()->traverse_parents([&nt,&child_data,&anynt](FlowNode* parent) {
                        click_chatter("VERIF Pruning %s %lu",parent->level()->print().c_str(),child_data);
                        bool changed;
                        if (nt.is_node())
                            nt = nt.node->prune(parent->level(), child_data,false, changed);
                        child_data = parent->node_data;
                    });
                    click_chatter("NT:");
                    nt.print();

                    click_chatter("ANYNT before:");
                    anynt.print();
*/
                    /**
                     * For each node of rule, we remove what matches
                     */
                    /*node->traverse_all_leaves([this,i,&anynt](FlowNodePtr* node){
                        anynt = anynt->remove_matching(node);
                    });
                    if (anynt.is_node())
                        anynt = anynt.node->prune(parent->level(), child_data,true, changed);
                    click_chatter("ANYNT:");
                    anynt.print();

                    (rules_copy[i].is_default?anynt:nt).traverse_all_leaves([this,rules_copy,i,nt,anynt](FlowNodePtr* cur){
                        if (!cur->leaf->data[_flow_data_offset] == rules_copy[i].output) {
                            click_chatter("VERIF Leaf does not lead to output %d :",rules_copy[i].output);
                            cur->leaf->print("",_flow_data_offset);
                            click_chatter("VERIF Pruned tree : ");
                            (rules_copy[i].is_default?anynt:nt).print(_flow_data_offset);
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
        }*/
#endif
	} else {
        _table->check();
    }
	//click_chatter("Table before duplicate : ");

	FlowNode* tmp = _table->duplicate(true,1, true);
	tmp->check();
	assert(tmp->has_no_default());
	return tmp;
}

void CTXDispatcher::push_flow(int, int* flowdata, PacketBatch* batch) {
	//click_chatter("%s : %d packets to output %d",name().c_str(),batch->count(),*flowdata);
	checked_output_push_batch(*flowdata, batch);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(CTXDispatcher)
EXPORT_ELEMENT(FlowContextDispatcher)
