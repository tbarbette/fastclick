/*
 * flownaptloadbalancer.{cc,hh} -- TCP & UDP load-balancer
 * Tom Barbette
 *
 * Copyright (c) 2017-2018 University of Liege
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

#include "flownaptloadbalancer.hh"


CLICK_DECLS

//TODO : disable timer if own_state is false

FlowNAPTLoadBalancer::FlowNAPTLoadBalancer() : _own_state(true), _accept_nonsyn(true) {

};

FlowNAPTLoadBalancer::~FlowNAPTLoadBalancer() {

}

int
FlowNAPTLoadBalancer::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
               .read_all("DST",Args::mandatory | Args::positional,DefaultArg<Vector<IPAddress>>(),_dsts)
               .read("SIP",Args::mandatory | Args::positional,DefaultArg<Vector<IPAddress>>(),_sips)
               .read("STATE", _own_state)
               .complete() < 0)
        return -1;

    click_chatter("%p{element} has %d routes and %d sources",this,_dsts.size(),_sips.size());

    if (_dsts.size() !=_sips.size())
        return errh->error("The number of SIP must match DST");

    return 0;
}


int FlowNAPTLoadBalancer::initialize(ErrorHandler *errh) {
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

#define FIRST_PORT 1024
#define LAST_PORT  65535

    /**
     * Now allocate ports for each thread
     */
    int total_ports = LAST_PORT - FIRST_PORT;
    int ports_per_thread = total_ports / passing.weight();
    int n = 0;
    for (int i = 0; i < passing.size(); i++) {
        if (!passing[i])
            continue;
        state &s = _state.get_value_for_thread(i);
        s.last = 0;

#if !HAVE_NAT_NEVER_REUSE
        s.min_port = FIRST_PORT + (n*ports_per_thread);
        s.max_port = s.min_port + ports_per_thread;
        s.ports.resize(_sips.size());
        for(int j = 0; j < _sips.size(); j++) {
            s.ports[j] = s.min_port;
        }
#else
        int min_port = FIRST_PORT + (n*ports_per_thread);
        int max_port = min_port + ports_per_thread;
        String r = name() + "#t" + String(i);
        s.available_ports.initialize(ports_per_thread, r.c_str());
        for (int port = min_port; port < max_port; port++) {
            s.available_ports.insert(new NATCommon(htons(port), &s.available_ports));
        }
#endif
        n++;
    }
    return 0;
}


NATCommon* FlowNAPTLoadBalancer::pick_port() {
    int i = 0;
    NATCommon* ref = 0;
    if (_state->available_ports.is_empty()) {
        click_chatter("%p{element} : Not even a used port available", this);
        return 0;
    } else {
        ref = _state->available_ports.extract();
    }
    return ref;
}

bool FlowNAPTLoadBalancer::new_flow(TTuple* flowdata, Packet* p) {
        if (!isSyn(p)) {
            nat_info_chatter("Non syn establishment!");

            if (!_accept_nonsyn || _own_state)
                return false;
        }
        state &s = *_state;
        int server = s.last++;
        if (s.last >= _dsts.size())
            s.last = 0;
        flowdata->pair.src = _sips[server];
        flowdata->pair.dst = _dsts[server];
        uint16_t my_port;
#if HAVE_NAT_NEVER_REUSE
        flowdata->ref = pick_port();
        if (!flowdata->ref) {
            click_chatter("ERROR %p{element} : no more ports available !",this);
            return false;
        }
        my_port = flowdata->ref->port;
        flowdata->ref->ref = 1;
        flowdata->ref->closing = 0;
        flowdata->fin_seen = false;
#else
        my_port = s.ports[server]++;

        flowdata->port = htons(my_port);
        if (s.ports[server] == s.max_port)
            s.ports[server] = s.min_port;
#endif
        nat_debug_chatter("New output %d, next is %d. New port is %d",server,s.last,ntohs(flowdata->get_port()));
        auto ip = p->ip_header();
        IPAddress osip = IPAddress(ip->ip_src);
        IPAddress odip = IPAddress(ip->ip_dst);
        auto th = p->tcp_header();
        assert(th);
        uint16_t osport = th->th_sport;
#if !HAVE_NAT_NEVER_REUSE
        LBEntry entry = LBEntry(flowdata->pair.dst, flowdata->port);
        _map.find_insert(entry, LBEntryOut(IPPair(odip,osip),osport));
#else
        LBEntry entry = LBEntry(flowdata->pair.dst, flowdata->ref->port);
        LBEntryOut out(IPPair(odip,osip),osport,flowdata->ref);
        assert(out.ref);
        _map.insert(entry, out,[](LBEntryOut& replaced){
             nat_info_chatter("Replacing never established %d", ntohs(replaced.ref->port) );
             //  release_ref(replaced.ref);
             //  The ref is still in one side's timeout, simply forget about it
        });
#endif
#if NAT_STATS
        s.conn++;
#endif
        nat_debug_chatter("Adding entry %s %d [%d] -> %d",entry.chosen_server.unparse().c_str(),ntohs(entry.port),s.last, ntohs(out.get_original_sport()));

        lb_assert(!flowdata->ref->closing);
        return true;
}

void FlowNAPTLoadBalancer::release_flow(TTuple* fcb) {
    release_ref(fcb->ref, _own_state);
}

void FlowNAPTLoadBalancer::push_flow(int, TTuple* flowdata, PacketBatch* batch) {
    nat_debug_chatter("Forward entry X:X -> %s:%d to %s:X", flowdata->pair.src.unparse().c_str(), ntohs(flowdata->get_port()), flowdata->pair.dst.unparse().c_str());
    state &s = *_state;

    auto fnt = [this,flowdata](Packet*&p) -> bool {
        WritablePacket* q =p->uniqueify();
        p = q;

        q->rewrite_ips_ports(flowdata->pair, flowdata->get_port(), 0);
        q->set_dst_ip_anno(flowdata->pair.dst);
        if (likely(!_own_state || likely(update_state<TTuple>(flowdata, q)))) {
            return true;
        } else {
            close_flow();
            release_flow(flowdata);
            return false;
        }
    };
    EXECUTE_FOR_EACH_PACKET_UNTIL_DROP(fnt, batch);

    if (batch)
        checked_output_push_batch(0, batch);
}


FlowNAPTLoadBalancerReverse::FlowNAPTLoadBalancerReverse() {

};

FlowNAPTLoadBalancerReverse::~FlowNAPTLoadBalancerReverse() {

}

int
FlowNAPTLoadBalancerReverse::configure(Vector<String> &conf, ErrorHandler *errh)
{
    Element* e;
    if (Args(conf, this, errh)
               .read_p("LB",e)
               .complete() < 0)
        return -1;
    _lb = reinterpret_cast<FlowNAPTLoadBalancer*>(e);
    _lb->add_remote_element(this);
    return 0;
}


int FlowNAPTLoadBalancerReverse::initialize(ErrorHandler *errh) {
    return 0;
}


//is_new?
//    if (flowdata->pair.src == IPAddress(0)) {
#if HAVE_NAT_NEVER_REUSE

bool FlowNAPTLoadBalancerReverse::new_flow(LBEntryOut* fcb, Packet* p) {
    auto ip = p->ip_header();
    auto th = p->tcp_header();
    LBEntry entry = LBEntry(ip->ip_src, th->th_dport);
    bool found = _lb->_map.find_erase(entry,*fcb);

    if (!isSyn(p)) {
        nat_info_chatter("Non syn answer!");
        //We need this one to get through, eg if SYN was sent to an established connection we'll get back an ACK, that the client must receive to understand its syn will not work.
//        return true; do not return, process normally, find the mapping, and wait for RST
    }

    if (unlikely(!found)) {
        nat_info_chatter("Could not find reverse entry %s:%d", entry.chosen_server.unparse().c_str(), ntohs(entry.port));
        //TODO : forge
        return false;
    } else {
        nat_debug_chatter("FOUND %d, port %d, osip %s osport %d, RST %d, FIN %d, SYN %d",found,ntohs(th->th_dport),fcb->get_original_sip().unparse().c_str(),ntohs(fcb->get_original_sport()), th->th_flags & TH_RST, th->th_flags & TH_FIN, th->th_flags & TH_SYN);

    }
    fcb->ref->ref++;
    fcb->fin_seen = false;
    if (unlikely(!fcb->ref) || fcb->ref->ref == 0) { //Connection was reset or timed out from the source itself
        return false;
    }
    lb_assert(fcb->ref->closing == 0);
#if NAT_STATS
    _lb->_state->open++;
#endif
    return true;
}

#else
bool FlowNAPTLoadBalancerReverse::new_flow(Packet* batch) {
        auto ip = batch->ip_header();
        auto th = batch->tcp_header();
        LBEntry entry = LBEntry(ip->ip_src, th->th_dport);
#if IPLOADBALANCER_MP
        bool found = _lb->_map.find_remove(entry,*flowdata);
        if (unlikely(!found)) {
            nat_info_chatter("Could not find %s %d",IPAddress(ip->ip_src).unparse().c_str(),th->th_dport);
            return false;
        } else {
            nat_debug_chatter("Found entry %s %d : %s -> %s",entry.chosen_server.unparse().c_str(),entry.port,ptr->pair.src.unparse().c_str(),ptr->pair.dst.unparse().c_str());
        }

#else
        LBHashtable::iterator ptr = _lb->_map.find(entry);

        if (!ptr) {

            nat_info_chatter("Could not find %s %d",IPAddress(ip->ip_src).unparse().c_str(),th->th_dport);
            //assert(false);
            //checked_output_push_batch(0, batch);
            return false;
        } else {
            //TODO : Delete?
#if DEBUG_NAT
            click_chatter("Found entry %s %d : %s -> %s",entry.chosen_server.unparse().c_str(),entry.port,ptr->pair.src.unparse().c_str(),ptr->pair.dst.unparse().c_str());
#endif
        }
        *flowdata = ptr.value();
#endif
#if NAT_STATS
    _lb->_stats->open++;
#endif
    return true;
}
#endif


void FlowNAPTLoadBalancerReverse::release_flow(LBEntryOut* fcb) {
    release_ref(fcb->ref,_lb->_own_state);
}

void FlowNAPTLoadBalancerReverse::push_flow(int, LBEntryOut* flowdata, PacketBatch* batch) {
        nat_debug_chatter("Saved entry X:%d -> %s:%d to %s:X",ntohs(flowdata->get_port()), flowdata->pair.src.unparse().c_str(), ntohs(flowdata->get_original_sport()), flowdata->pair.dst.unparse().c_str());

    auto fnt = [this,flowdata](Packet* &p) -> bool {
        WritablePacket* q =p->uniqueify();
        p = q;
        q->rewrite_ips_ports(flowdata->pair, 0, flowdata->get_original_sport());
        q->set_dst_ip_anno(flowdata->pair.dst);
        if (likely(!_lb->_own_state || likely(update_state<TTuple>(flowdata, q)))) {
            return true;
        } else {
            close_flow();
            release_flow(flowdata);
            return false;
        }
    };

    EXECUTE_FOR_EACH_PACKET_UNTIL_DROP(fnt, batch);

    if (batch)
        checked_output_push_batch(0, batch);
}

enum {h_conn, h_open};
String FlowNAPTLoadBalancer::read_handler(Element* e, void* thunk) {
    FlowNAPTLoadBalancer* tc = static_cast<FlowNAPTLoadBalancer*>(e);

    switch ((intptr_t)thunk) {
#if NAT_STATS
        case h_conn: {
            PER_THREAD_MEMBER_SUM(unsigned long long, conn, tc->_state, conn);
            return String(conn);
                     }
        case h_open: {
            PER_THREAD_MEMBER_SUM(unsigned long long, open, tc->_state, open);
            return String(open);
                     }
#endif
    }
    return String("");
}

void FlowNAPTLoadBalancer::add_handlers() {
#if NAT_STATS
    add_read_handler("conn", read_handler, h_conn);
    add_read_handler("open", read_handler, h_open);
#endif
}



CLICK_ENDDECLS
ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(FlowNAPTLoadBalancerReverse)
ELEMENT_MT_SAFE(FlowNAPTLoadBalancerReverse)
EXPORT_ELEMENT(FlowNAPTLoadBalancer)
ELEMENT_MT_SAFE(FlowNAPTLoadBalancer)
