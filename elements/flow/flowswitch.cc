/*
 * flowswitch.{cc,hh} -- TCP & UDP load-balancer
 * Tom Barbette
 *
 * Copyright (c) 2019-2020 KTH Royal Institute of Technology
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
#include <click/glue.hh>
#include <click/args.hh>
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include <click/flow/flow.hh>

#include "flowswitch.hh"


CLICK_DECLS

FlowSwitch::FlowSwitch()
{
}

FlowSwitch::~FlowSwitch()
{
}

int
FlowSwitch::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(this, errh).bind(conf)
               .consume() < 0)
		return -1;

    _dsts.resize(noutputs());

    if (parseLb(conf, this, errh) < 0)
            return -1;

    if (Args(this, errh).bind(conf).complete() < 0)
            return -1;

    click_chatter("%p{element} has %d routes",this,_dsts.size());

    return 0;
}

int FlowSwitch::initialize(ErrorHandler *errh)
{
    return 0;
}


bool FlowSwitch::new_flow(FlowSwitchEntry* flowdata, Packet* p)
{
    int server = pick_server(p);

    flowdata->chosen_server = server;

    return true;
}

void FlowSwitch::push_batch(int, FlowSwitchEntry* flowdata, PacketBatch* batch)
{
    output_push_batch(flowdata->chosen_server, batch);
}

int
FlowSwitch::handler(int op, String& s, Element* e, const Handler* h, ErrorHandler* errh) {
    FlowSwitch *cs = static_cast<FlowSwitch *>(e);
    return cs->lb_handler(op, s, h->read_user_data(), h->write_user_data(), errh);
}

int
FlowSwitch::write_handler(
        const String &input, Element *e, void *thunk, ErrorHandler *errh) {
	FlowSwitch *cs = static_cast<FlowSwitch *>(e);

    return cs->lb_write_handler(input,thunk,errh);
}

String
FlowSwitch::read_handler(Element *e, void *thunk) {
	FlowSwitch *cs = static_cast<FlowSwitch *>(e);
    return cs->lb_read_handler(thunk);
}

void
FlowSwitch::add_handlers()
{
    add_lb_handlers<FlowSwitch>(this);
}


CLICK_ENDDECLS
EXPORT_ELEMENT(FlowSwitch)
ELEMENT_MT_SAFE(FlowSwitch)
