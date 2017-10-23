// -*- c-basic-offset: 4; related-file-name: "../include/click/flowelement.hh" -*-
/*
 * flowelement.{cc,hh} -- the FlowElement base class
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
#include <click/flowelement.hh>


CLICK_DECLS

#ifdef HAVE_FLOW

FlowNode*
FlowElementVisitor::get_downward_table(Element* e,int output,FlowElement* context) {
	FlowNode* merged = 0;

    FlowElementVisitor v(e);

    e->router()->visit(e,true,output,&v);
#if DEBUG_CLASSIFIER
    if (v.dispatchers.size() > 0)
        click_chatter("Children of %p{element}[%d] : ",e,output);
    else
        click_chatter("%p{element}[%d] has no children",e,output);
#endif
    //TODO : Do not catch all bugs
    for (int i = 0; i < v.dispatchers.size(); i++) {
        //click_chatter("%p{element}",v.dispatchers[i].elem);
		if (v.dispatchers[i].elem == (FlowElement*)e) {
			click_chatter("Classification loops are unsupported, place another FlowClassifier before reinjection of the packets.");
			e->router()->please_stop_driver();
			return 0;
		}
		//
		FlowNode* child_table = context->resolveContext(v.dispatchers[i].elem->getContext())->combine(v.dispatchers[i].elem->get_table(v.dispatchers[i].iport, context),true,true);
#if DEBUG_CLASSIFIER
		click_chatter("Traversal merging of %p{element} [%d] : ",v.dispatchers[i].elem,v.dispatchers[i].iport);
		if (child_table) {
		    child_table->print();
		} else {
		    click_chatter("--> No table !");
		}
		if (merged) {
		    click_chatter("With :");
		    merged->print();
		} else {
		    click_chatter("With NONE");
		}
#endif
		if (merged)
			merged = merged->combine(child_table, false, false);
		else
			merged = child_table;
		if (merged) {
#if DEBUG_CLASSIFIER
		    click_chatter("Merged traversal[%d] with %p{element}",i,v.dispatchers[i].elem);
#endif
		    merged->debug_print();
		    merged->check();
            flow_assert(merged->is_full_dummy() || !merged->is_dummy());
		    //assert(merged->has_no_default());
		}
	}
#if DEBUG_CLASSIFIER
    click_chatter("Traversal finished");
#endif
	return merged;
}

FlowElement::FlowElement() : _classifier(0) {
    if (flow_code() != Element::COMPLETE_FLOW) {
        click_chatter("Flow Elements must be x/x in their flows");
        assert(flow_code() == Element::COMPLETE_FLOW);
    }
    if (in_batch_mode < BATCH_MODE_NEEDED)
        in_batch_mode = BATCH_MODE_NEEDED;
};

FlowElement::~FlowElement() {

};

FlowNode*
FlowElement::get_table(int iport, FlowElement* context) {

    return FlowElementVisitor::get_downward_table(this,-1,context);
}

FlowNode*
FlowElement::resolveContext(FlowType) {
    return FlowClassificationTable::make_drop_rule().root;
}

FlowType  FlowElement::getContext() {
    return FLOW_NONE;
}

#endif
CLICK_ENDDECLS
