/*
 * flowiploadbalancer.{cc,hh}
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/flow.hh>
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include "flowiploadbalancer.hh"

CLICK_DECLS

#define DEBUG_LB 0

#define LOADBALANCER_FLOW_TIMEOUT 60 * 1000

FlowIPLoadBalancer::FlowIPLoadBalancer() {

};

FlowIPLoadBalancer::~FlowIPLoadBalancer() {

}

int
FlowIPLoadBalancer::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
               .read_all("DST",Args::mandatory | Args::positional,DefaultArg<Vector<IPAddress>>(),_dsts)
               .read("SIP",Args::mandatory | Args::positional,DefaultArg<Vector<IPAddress>>(),_sips)
               .complete() < 0)
        return -1;
    click_chatter("%p{element} has %d routes and %d sources",this,_dsts.size(),_sips.size());
    if (_dsts.size() !=_sips.size())
        return errh->error("The number of SIP must match DST");
    return 0;
}


int FlowIPLoadBalancer::initialize(ErrorHandler *errh) {
    Bitvector passing = get_passing_threads();

    if (passing.weight() <= 1) {
        _map.disable_mt();
    }
    if (passing.weight() == 0) {
        return errh->warning("No thread passing by, element will not work if it's not indeed idle");
    }

    int total_ports = 65536 - 1024;
    int ports_per_thread = total_ports / passing.weight();
    int n = 0;
    for (int i = 0; i < passing.size(); i++) {
        if (!passing[i])
            continue;
        state &s = _state.get_value_for_thread(i);
        s.last = 0;
        s.min_port = 1024 + (n*ports_per_thread);
        s.max_port = s.min_port + ports_per_thread;
        s.ports.resize(_sips.size());
        for(int j = 0; j < _sips.size(); j++) {
            s.ports[j] = s.min_port;
        }
        n++;
    }
    return 0;
}


void FlowIPLoadBalancer::push_batch(int, TTuple* flowdata, PacketBatch* batch) {

    state &s = *_state;
    if (flowdata->pair.src == IPAddress(0)) {
        int server = s.last++;
        if (s.last >= _dsts.size())
            s.last = 0;
        flowdata->pair.src = _sips[server];
        flowdata->pair.dst = _dsts[server];
        flowdata->port = htons(s.ports[server]++);
        if (s.ports[server] == s.max_port)
            s.ports[server] = s.min_port;
#if DEBUG_LB
        click_chatter("New output %d, next is %d. New port is %d",server,s.last,ntohs(flowdata->port));
#endif
        auto ip = batch->ip_header();
        IPAddress osip = IPAddress(ip->ip_src);
        IPAddress odip = IPAddress(ip->ip_dst);
        auto th = batch->tcp_header();
        assert(th);
        uint16_t osport = th->th_sport;
        LBEntry entry = LBEntry(flowdata->pair.dst, flowdata->port);
        _map.find_insert(entry, TTuple(IPPair(odip,osip),osport));
#if DEBUG_LB
        click_chatter("Adding entry %s %d [%d]",entry.chosen_server.unparse().c_str(),entry.port,s.last);
#endif
        fcb_acquire_timeout(LOADBALANCER_FLOW_TIMEOUT);
    } else {
#if DEBUG_CLASSIFIER_TIMEOUT > 1
        if (!fcb_stack->hasTimeout())
            click_chatter("Forward received without timeout?");
#endif
    }

    auto fnt = [this,flowdata](Packet*p) -> Packet* {
        WritablePacket* q=p->uniqueify();
        q->rewrite_ips_ports(flowdata->pair, flowdata->port, 0);
        q->set_dst_ip_anno(flowdata->pair.dst);
        if ((q->tcp_header()->th_flags & TH_RST) || ((q->tcp_header()->th_flags & TH_FIN) && (q->tcp_header()->th_flags | TH_ACK))) {
#if DEBUG_LB || DEBUG_CLASSIFIER_TIMEOUT > 1
            click_chatter("Forward Rst %d, fin %d, ack %d",(q->tcp_header()->th_flags & TH_RST), (q->tcp_header()->th_flags & (TH_FIN)), (q->tcp_header()->th_flags & (TH_ACK)));
#endif
            fcb_release_timeout();
        }
        return q;
    };
    EXECUTE_FOR_EACH_PACKET(fnt, batch);

    checked_output_push_batch(0, batch);
}

FlowIPLoadBalancerReverse::FlowIPLoadBalancerReverse() {

};

FlowIPLoadBalancerReverse::~FlowIPLoadBalancerReverse() {

}

int
FlowIPLoadBalancerReverse::configure(Vector<String> &conf, ErrorHandler *errh)
{
    Element* e;
    if (Args(conf, this, errh)
               .read_p("LB",e)
               .complete() < 0)
        return -1;
    _lb = reinterpret_cast<FlowIPLoadBalancer*>(e);
    _lb->add_remote_element(this);
    return 0;
}


int FlowIPLoadBalancerReverse::initialize(ErrorHandler *errh) {

    return 0;
}




void FlowIPLoadBalancerReverse::push_batch(int, TTuple* flowdata, PacketBatch* batch) {
    if (flowdata->pair.src == IPAddress(0)) {
        auto ip = batch->ip_header();
        auto th = batch->tcp_header();
        LBEntry entry = LBEntry(ip->ip_src, th->th_dport);
#if IPLOADBALANCER_MP
        LBHashtable::ptr ptr = _lb->_map.find(entry);
#else
        LBHashtable::iterator ptr = _lb->_map.find(entry);
#endif
        if (!ptr) {

#if DEBUG_LB
            click_chatter("Could not find %s %d",IPAddress(ip->ip_src).unparse().c_str(),th->th_dport);
#endif
            //assert(false);
            //checked_output_push_batch(0, batch);
            batch->kill();
            return;
        } else {
            //TODO : Delete?
#if DEBUG_LB
            click_chatter("Found entry %s %d : %s -> %s",entry.chosen_server.unparse().c_str(),entry.port,ptr->pair.src.unparse().c_str(),ptr->pair.dst.unparse().c_str());
#endif
        }
#if IPLOADBALANCER_MP
        *flowdata = *ptr;
#else
        *flowdata = ptr.value();
#endif
        fcb_acquire_timeout(LOADBALANCER_FLOW_TIMEOUT);
    } else {
#if DEBUG_LB
        click_chatter("Saved entry %s -> %s",flowdata->pair.src.unparse().c_str(),flowdata->pair.dst.unparse().c_str());
#endif
#if DEBUG_CLASSIFIER_TIMEOUT > 1
        if (!fcb_stack->hasTimeout()) {
            click_chatter("Reverse received without timeout?");
        }
#endif
    }


    auto fnt = [this,flowdata](Packet*p) -> Packet*{
        WritablePacket* q=p->uniqueify();
        q->rewrite_ips_ports(flowdata->pair, 0, flowdata->port);
        q->set_dst_ip_anno(flowdata->pair.dst);
        if ((q->tcp_header()->th_flags & TH_RST) || ((q->tcp_header()->th_flags & TH_FIN) && (q->tcp_header()->th_flags | TH_ACK))) {
#if DEBUG_LB || DEBUG_CLASSIFIER_TIMEOUT > 1
            click_chatter("Reverse Rst %d, fin %d, ack %d",(q->tcp_header()->th_flags & TH_RST), (q->tcp_header()->th_flags & (TH_FIN)), (q->tcp_header()->th_flags & (TH_ACK)));
#endif
            fcb_release_timeout();
        }
        return q;
    };

    EXECUTE_FOR_EACH_PACKET(fnt, batch);


    checked_output_push_batch(0, batch);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(FlowIPLoadBalancerReverse)
ELEMENT_MT_SAFE(FlowIPLoadBalancerReverse)
EXPORT_ELEMENT(FlowIPLoadBalancer)
ELEMENT_MT_SAFE(FlowIPLoadBalancer)
