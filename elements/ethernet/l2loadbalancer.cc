/*
 * L2LoadBalancer.{cc,hh} -- TCP & UDP load-balancer
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
#include "l2loadbalancer.hh"


CLICK_DECLS

//TODO : disable timer if own_state is false

L2LoadBalancer::L2LoadBalancer()
{
}

L2LoadBalancer::~L2LoadBalancer()
{
}

int
L2LoadBalancer::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String mode= "hash";
    if (Args(this, errh).bind(conf)
               .read_all("DST",Args::mandatory | Args::positional,DefaultArg<Vector<EtherAddress>>(),_dsts)
               .consume() < 0)
		return -1;

    if (parseLb(conf, this, errh) < 0)
            return -1;

    if (Args(this, errh).bind(conf).complete() < 0)
            return -1;

    click_chatter("%p{element} has %d routes",this,_dsts.size());
    return 0;
}

int L2LoadBalancer::initialize(ErrorHandler *errh)
{
    return 0;
}

void L2LoadBalancer::push_batch(int, PacketBatch* batch)
{
    auto fnt = [this](Packet*&p) -> bool {
        WritablePacket* q =p->uniqueify();
        p = q;

        int server = pick_server(p);


        EtherAddress srv = _dsts[server];

        memcpy(&q->ether_header()->ether_dhost, srv.data(), sizeof(EtherAddress));
        return true;
    };
    EXECUTE_FOR_EACH_PACKET_UNTIL_DROP(fnt, batch);

    if (batch)
        checked_output_push_batch(0, batch);
}



CLICK_ENDDECLS
EXPORT_ELEMENT(L2LoadBalancer)
ELEMENT_MT_SAFE(L2LoadBalancer)
