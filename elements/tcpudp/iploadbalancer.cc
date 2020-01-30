/*
 * iploadbalancer.{cc,hh} -- TCP & UDP load-balancer
 * Tom Barbette
 *
 * Copyright (c) 2019 KTH Royal Institute of Technology
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

#include "iploadbalancer.hh"

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/ipflowid.hh>
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include <click/packetbatch.hh>


CLICK_DECLS

//TODO : disable timer if own_state is false

IPLoadBalancer::IPLoadBalancer() {

};

IPLoadBalancer::~IPLoadBalancer() {

}

int
IPLoadBalancer::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
               .read_all("DST",Args::mandatory | Args::positional,DefaultArg<Vector<IPAddress>>(),_dsts)
               .read_mp("VIP", _vip)
               .complete() < 0)
        return -1;
    click_chatter("%p{element} has %d routes",this,_dsts.size());

    return 0;
}


int IPLoadBalancer::initialize(ErrorHandler *errh) {

    return 0;
}

#if HAVE_BATCH
void IPLoadBalancer::push_batch(int, PacketBatch* batch) {

    auto fnt = [this](Packet*&p) {
        WritablePacket* q =p->uniqueify();
        p = q;

        unsigned hash = pick_server(p);
        IPAddress srv = _dsts.unchecked_at(hash);

	    q->ip_header()->ip_dst = srv;
        q->set_dst_ip_anno(srv);
        return q;
    };
    EXECUTE_FOR_EACH_PACKET(fnt, batch);

    if (batch)
        checked_output_push_batch(0, batch);
}
#endif

void IPLoadBalancer::push(int, Packet* p) {
        WritablePacket* q =p->uniqueify();

        if (!q)
            return;

        unsigned hash = pick_server(p);
        IPAddress srv = _dsts.unchecked_at(hash);

	q->ip_header()->ip_dst = srv;
        q->set_dst_ip_anno(srv);

        output_push(0, q);
}

IPLoadBalancerReverse::IPLoadBalancerReverse() {

};

IPLoadBalancerReverse::~IPLoadBalancerReverse() {

}

int
IPLoadBalancerReverse::configure(Vector<String> &conf, ErrorHandler *errh)
{
    Element* e;
    if (Args(conf, this, errh)
               .read_p("LB",e)
               .complete() < 0)
        return -1;
    _lb = reinterpret_cast<IPLoadBalancer*>(e);
    _lb->add_remote_element(this);
    return 0;
}


int IPLoadBalancerReverse::initialize(ErrorHandler *errh) {
    return 0;
}

#if HAVE_BATCH
void IPLoadBalancerReverse::push_batch(int, PacketBatch* batch) {

    auto fnt = [this](Packet* &p)  {
        WritablePacket* q =p->uniqueify();
        p = q;
	q->ip_header()->ip_src = _lb->_vip;
        return q;
    };


    EXECUTE_FOR_EACH_PACKET(fnt, batch);

    if (batch)
        checked_output_push_batch(0, batch);
}
#endif

void IPLoadBalancerReverse::push(int, Packet* p) {
        WritablePacket* q =p->uniqueify();
        if (!q)
            return;
	    q->ip_header()->ip_src = _lb->_vip;

        output_push(0, q);
}

CLICK_ENDDECLS

EXPORT_ELEMENT(IPLoadBalancerReverse)
ELEMENT_MT_SAFE(IPLoadBalancerReverse)
EXPORT_ELEMENT(IPLoadBalancer)
ELEMENT_MT_SAFE(IPLoadBalancer)
