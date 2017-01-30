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
FlowElementVisitor::get_downward_table(Element* e,int output) {
	FlowNode* merged = 0;

    FlowElementVisitor v(e);

    e->router()->visit(e,true,output,&v);
    click_chatter("Children of %p{element} : ",e);
    for (int i = 0; i < v.dispatchers.size(); i++) {
        click_chatter("%p{element}",v.dispatchers[i]);
		if (v.dispatchers[i] == (FlowElement*)e) {
			click_chatter("Classification loops are unsupported, place another FlowClassifier before reinjection of the packets.");
			e->router()->please_stop_driver();
			return 0;
		}
		if (merged)
			merged = merged->combine(v.dispatchers[i]->get_table());
		else
			merged = v.dispatchers[i]->get_table();
	}
	return merged;
}

FlowNode*
FlowElement::get_table() {
	return FlowElementVisitor::get_downward_table(this,-1);
}
#endif
CLICK_ENDDECLS
