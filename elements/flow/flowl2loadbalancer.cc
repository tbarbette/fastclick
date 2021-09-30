/*
 * FlowL2LoadBalancer.{cc,hh} -- TCP & UDP load-balancer
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
#include <clicknet/ether.h>
#include <click/flow/flow.hh>

#include "flowl2loadbalancer.hh"


CLICK_DECLS


FlowL2LoadBalancer::FlowL2LoadBalancer()
{
}

FlowL2LoadBalancer::~FlowL2LoadBalancer()
{
}

int
FlowL2LoadBalancer::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
       .read_all("DST",Args::mandatory | Args::positional,DefaultArg<Vector<EtherAddress>>(),_dsts)
       .complete() < 0)
        return -1;

    if (parseLb(conf, this, errh) < 0)
            return -1;

    if (Args(this, errh).bind(conf).complete() < 0)
            return -1;

    click_chatter("%p{element} has %d routes",this,_dsts.size());

    return 0;
}

int FlowL2LoadBalancer::initialize(ErrorHandler *errh)
{
    return 0;
}


bool FlowL2LoadBalancer::new_flow(L2LBEntry* flowdata, Packet* p)
{
    int server = pick_server(p);

    flowdata->chosen_server = server;

    nat_debug_chatter("New flow %d",server);

    return true;
}


void FlowL2LoadBalancer::push_flow(int, L2LBEntry* flowdata, PacketBatch* batch)
{
    auto fnt = [this,flowdata](Packet*&p) -> bool {
        WritablePacket* q =p->uniqueify();
        p = q;

        nat_debug_chatter("Packet for flow %d", flowdata->chosen_server);
        EtherAddress srv = _dsts[flowdata->chosen_server];

        memcpy(&q->ether_header()->ether_dhost, srv.data(), sizeof(EtherAddress));
        return true;
    };
    EXECUTE_FOR_EACH_PACKET_UNTIL_DROP(fnt, batch);

    if (batch)
        checked_output_push_batch(0, batch);
}



CLICK_ENDDECLS
EXPORT_ELEMENT(FlowL2LoadBalancer)
ELEMENT_MT_SAFE(FlowL2LoadBalancer)
