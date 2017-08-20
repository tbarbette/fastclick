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

FlowIPLoadBalancer::FlowIPLoadBalancer() : _last(0){

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

    return 0;
}




void FlowIPLoadBalancer::push_batch(int, IPPair* flowdata, PacketBatch* batch) {

    if ((*flowdata).src == IPAddress(0)) {
        int server = _last++;
        if (*_last >= _dsts.size())
            *_last = 0;
        flowdata->src = _sips[server];
        flowdata->dst = _dsts[server];
#if DEBUG_LB
        click_chatter("New output %d, next is %d",server,*_last);
#endif
        auto ip = batch->ip_header();
        IPAddress osip = IPAddress(ip->ip_src);
        IPAddress odip = IPAddress(ip->ip_dst);
        auto th = batch->tcp_header();
        assert(th);
        LBEntry entry = LBEntry(flowdata->dst, th->th_sport);
        _map.find_insert(entry, IPPair(odip,osip));
#if DEBUG_LB
        click_chatter("Adding entry %s %d [%d]",entry.chosen_server.unparse().c_str(),entry.port,*_last);
#endif
        fcb_acquire_timeout(LOADBALANCER_FLOW_TIMEOUT);
    } else {
#if DEBUG_CLASSIFIER_TIMEOUT > 1
        if (!fcb_stack->hasTimeout())
            click_chatter("Forward received without timeout?");
#endif
    }

    auto fnt = [flowdata,this](Packet*p) -> Packet*{
        WritablePacket* q=p->uniqueify();
        q->rewrite_ips(*flowdata);
        q->set_dst_ip_anno(flowdata->dst);
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
    return 0;
}


int FlowIPLoadBalancerReverse::initialize(ErrorHandler *errh) {

    return 0;
}




void FlowIPLoadBalancerReverse::push_batch(int, IPPair* flowdata, PacketBatch* batch) {
    if (flowdata->src == IPAddress(0)) {
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
        } else{
#if DEBUG_LB
            click_chatter("Found entry %s %d : %s -> %s",entry.chosen_server.unparse().c_str(),entry.port,ptr->src.unparse().c_str(),ptr->dst.unparse().c_str());
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
        click_chatter("Saved entry %s -> %s",flowdata->src.unparse().c_str(),flowdata->dst.unparse().c_str());
#endif
#if DEBUG_CLASSIFIER_TIMEOUT > 1
        if (!fcb_stack->hasTimeout()) {
            click_chatter("Reverse received without timeout?");
        }
#endif
    }


    auto fnt = [this,flowdata](Packet*p) -> Packet*{
        WritablePacket* q=p->uniqueify();
        q->rewrite_ips(*flowdata);
        q->set_dst_ip_anno(flowdata->dst);
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
