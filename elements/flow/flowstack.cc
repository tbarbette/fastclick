// -*- mode: c++; c-basic-offset: 4 -*-
/*
 * FlowStack.{cc,hh} -- Put flow info on the stack, and push in a clean env
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include <click/args.hh>
#include <click/error.hh>
#include <click/glue.hh>

#include "flowstack.hh"
CLICK_DECLS

FlowStack::FlowStack() : _release(true)
{
}

int
FlowStack::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return Args(conf, this, errh).
            read_p("RELEASE", _release)
            .complete();
}

void
FlowStack::push_batch(int, PacketBatch *head)
{
    if (_release && fcb_stack) {
        fcb_stack->release(head->count());
    }
    FlowControlBlock* tmp_stack = fcb_stack;
    fcb_stack = 0;
    output_push_batch(0, head);
    fcb_stack = tmp_stack;
}



CLICK_ENDDECLS
ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(FlowStack)
ELEMENT_MT_SAFE(FlowStack)
