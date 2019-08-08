// -*- c-basic-offset: 4; related-file-name: "../include/click/flow/flowelement.hh" -*-
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
#include <click/flow/flowelement.hh>


CLICK_DECLS

#ifdef HAVE_FLOW

FlowElement::FlowElement()  {
    if (flow_code() != Element::COMPLETE_FLOW) {
        click_chatter("Flow Elements must be x/x in their flows");
        assert(flow_code() == Element::COMPLETE_FLOW);
    }
    if (in_batch_mode < BATCH_MODE_NEEDED)
        in_batch_mode = BATCH_MODE_NEEDED;
};

FlowElement::~FlowElement() {

};

FlowType  FlowElement::getContext() {
    return FLOW_NONE;
}

void *
VirtualFlowSpaceElement::cast(const char *name) {
    if (strcmp("VirtualFlowSpaceElement", name) == 0) {
        return this;
    }
    return FlowElement::cast(name);
}

#endif
CLICK_ENDDECLS
