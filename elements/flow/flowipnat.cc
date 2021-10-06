/*
 * FlowIPNAT.{cc,hh}
 * Copyright (c) 2019-2020 Tom Barbette, KTH Royal Institute of Technology
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
#include "flowipnat.hh"
#include <click/flow/flow.hh>

CLICK_DECLS

FlowIPNAT::FlowIPNAT() : _sip(), _map(65536), _accept_nonsyn(true), _own_state(false)
{
}

FlowIPNAT::~FlowIPNAT()
{
}

int
FlowIPNAT::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
       .read_mp("SIP",_sip)
       .read_or_set("ACCEPT_NONSYN", _accept_nonsyn, true)
       .read_or_set("STATE", _own_state, false)
       .complete() < 0)
        return -1;

    return 0;
}


int FlowIPNAT::initialize(ErrorHandler *errh) {
    /* Get "touching" threads. That is threads passing by us and touching
     * our state.
     * NATReverse takes care of telling that it will touch our hashtable
     * therefore touching is actually the passing threads for both directions
     */
    Bitvector touching = get_passing_threads(true);

    /**
     * If only one thread touch this element, disable MT-safeness.
     */
    if (touching.weight() <= 1) {
        _map.disable_mt();
    }

    /**
     * Get passing threads, that is the threads that will call push_flow
     */
    Bitvector passing = get_passing_threads(false);
    if (passing.weight() == 0) {
        return errh->warning("No thread passing by, element will not work if it's not indeed idle");
    }

    /**
     * Now allocate ports for each thread
     */
    int total_ports = 65536 - 1024;
    int ports_per_thread = total_ports / passing.weight();
    int n = 0;
    for (int i = 0; i < passing.size(); i++) {
        if (!passing[i])
            continue;
        state &s = _state.get_value_for_thread(i);
        int min_port = 1024 + (n*ports_per_thread);
        int max_port = min_port + ports_per_thread;
        String rn = name() + "-t#" + String(i);
        s.available_ports.initialize(ports_per_thread, rn.c_str());
        for (int port = min_port; port < max_port; port++) {
            NATCommon* ref = new NATCommon(htons(port), &s.available_ports);
            assert(ref->ref == 0);
            s.available_ports.insert(ref);
        }
        n++;
    }
    return 0;
}

NATCommon* FlowIPNAT::pick_port()
{
    int i = 0;
    NATCommon* ref = 0;
    if (_state->available_ports.is_empty()) {
        click_chatter("%p{element} : Not even a used port available", this);
        return 0;
    } else {
        ref = _state->available_ports.extract();
#if NAT_COLLIDE
        if (ref->ref.value() != 0) {
            click_chatter("Ref is %d", ref->ref.value());
            assert(ref->ref.value() == 0);
        }
        /*while (ref->ref.value() > 0)
        {
            _state->available_ports.insert(ref);
            if (++i > _state->available_ports.count()) { //Full loop, stop here
                click_chatter("All ports are referenced...");
                return 0;
            }
            ref = _state->available_ports.extract();
        }*/
#else
        //Reinsert the reference at the back
        static_assert(false);
        _state->available_ports.insert(ref);
#endif
    }
    return ref;
}

bool FlowIPNAT::new_flow(NATEntryIN* fcb, Packet* p)
{
    if (!_accept_nonsyn && !p->tcp_header()->th_flags & TH_SYN) {
        click_chatter("Flow does not start with a SYN...");
        return false;
    }
    IPAddress osip = IPAddress(p->ip_header()->ip_src);
    uint16_t oport = p->tcp_header()->th_sport;
    fcb->ref = pick_port();
    if (!fcb->ref) {
        click_chatter("ERROR %p{element} : no more ports available !",this);
        return false;
    }
#if NAT_COLLIDE
    fcb->ref->ref = 2; //One for us, one for the table
    fcb->ref->closing = false;
    fcb->fin_seen = false;
#endif
    //click_chatter("NEW osip %s osport %d, new port %d",osip.unparse().c_str(),htons(oport),ntohs(fcb->ref->port));
    NATEntryOUT out(IPPort(osip,oport),fcb->ref);
    _map.insert(fcb->ref->port, out,[this](NATEntryOUT& replaced){
        release_ref(replaced.ref, _own_state);
    });
    return true;
}

void FlowIPNAT::release_flow(NATEntryIN* fcb)
{
    // click_chatter("Release %d", ntohs(fcb->ref->port));
#if NAT_COLLIDE
    release_ref(fcb->ref, _own_state);
#else
    _state->available_ports.insert(fcb->ref);
    fcb->ref = 0;
#endif
}


void FlowIPNAT::push_flow(int port, NATEntryIN* flowdata, PacketBatch* batch)
{
    if (!_own_state && flowdata->ref && flowdata->ref->closing && isSyn(batch->first())) {
        // If the state is not handled by us, another manager could have deleted the other side while
        // this side has still a handle.
        release_ref(flowdata->ref, _own_state);
        new_flow(flowdata, batch->first());
    }
    auto fnt = [this,flowdata](Packet* &p) -> bool {
        if (!flowdata->ref) {
            return false;
        }

        WritablePacket* q=p->uniqueify();
        //click_chatter("Rewrite to %s %d",_sip.unparse().c_str(),htons(flowdata->ref->port));
        q->rewrite_ipport(_sip, flowdata->ref->port, 0, isTCP(q));
        p = q;

        if (!_own_state || update_state<NATState>(flowdata,q)) {
            return true;
        } else {
            close_flow();
            release_flow(flowdata);
            return false;
        }
    };
    EXECUTE_FOR_EACH_PACKET_UNTIL_DROP(fnt, batch);

    output_push_batch(0, batch);
}

FlowIPNATReverse::FlowIPNATReverse()
{
}

FlowIPNATReverse::~FlowIPNATReverse()
{
}


int
FlowIPNATReverse::configure(Vector<String> &conf, ErrorHandler *errh)
{
    Element* e;
    if (Args(conf, this, errh)
        .read_mp("NAT",e)
        .complete() < 0)
        return -1;
    _in = reinterpret_cast<FlowIPNAT*>(e);
    return 0;
}

bool FlowIPNATReverse::new_flow(NATEntryOUT* fcb, Packet* p)
{
    auto th = p->tcp_header();

    bool found = _in->_map.find_erase(th->th_dport,*fcb);

    if (unlikely(!found)) {
        //click_chatter("NOT FOUND %d, %d, osip %s osport %d, RST %d, FIN %d, SYN %d",found,ntohs(th->th_dport),fcb->map.ip.unparse().c_str(),ntohs(fcb->map.port), th->th_flags & TH_RST, th->th_flags & TH_FIN, th->th_flags & TH_SYN);
        return false;
    }

    //No ref++, we keep the table reference
    lb_assert(fcb->ref->ref > 0);
    if (fcb->ref->ref == 1) { //Connection was reset by the inserter
        click_chatter("Got a closed connection");
        release_ref(fcb->ref, _in->_own_state);
        return false;
    }
    return true;
}

void FlowIPNATReverse::release_flow(NATEntryOUT* fcb) {
//    click_chatter("Release rvr %d",ntohs(fcb->ref->port));

#if NAT_COLLIDE
	release_ref(fcb->ref, _in->_own_state);
#else
    _state->available_ports.insert(fcb->ref);
    fcb->ref = 0;
#endif

}

void FlowIPNATReverse::push_flow(int port, NATEntryOUT* flowdata, PacketBatch* batch)
{
    if (!_in->_own_state && flowdata->ref && flowdata->ref->closing && isSyn(batch->first())) {
        // If the state is not handled by us, another manager could have deleted the other side while
        // this side has still a handle.
        release_ref(flowdata->ref, _in->_own_state);
        new_flow(flowdata, batch->first());
    }

    //_state->port_epoch[*flowdata] = epoch;
    auto fnt = [this,flowdata](Packet* &p) -> bool {
        //click_chatter("Rewrite to %s %d",flowdata->map.ip.unparse().c_str(),ntohs(flowdata->map.port));
        if (!flowdata->ref) {
            click_chatter("Return flow without ref?");
            return false;
        }
        WritablePacket* q=p->uniqueify();
        p = q;
        q->rewrite_ipport(flowdata->map.ip, flowdata->map.port, 1, isTCP(q));
        q->set_dst_ip_anno(flowdata->map.ip);

        if (!_in->_own_state || update_state<NATState>(flowdata, q)) {
            return true;
        } else {
            close_flow();
            release_flow(flowdata);
            return false;
        }

    };
    EXECUTE_FOR_EACH_PACKET_UNTIL_DROP(fnt, batch);

    output_push_batch(0, batch);
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(FlowIPNATReverse)
ELEMENT_MT_SAFE(FlowIPNATReverse)
EXPORT_ELEMENT(FlowIPNAT)
ELEMENT_MT_SAFE(FlowIPNAT)
