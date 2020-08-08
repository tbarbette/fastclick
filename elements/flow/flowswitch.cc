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
    String mode= "rr";
    if (Args(conf, this, errh)
       .read("MODE", mode)
       .complete() < 0)
        return -1;

    _dsts.resize(noutputs());
    click_chatter("%p{element} has %d routes",this,_dsts.size());

    set_mode(mode);
    click_chatter("MODE set to %s", mode.c_str());

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


CLICK_ENDDECLS
EXPORT_ELEMENT(FlowSwitch)
ELEMENT_MT_SAFE(FlowSwitch)
