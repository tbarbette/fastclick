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

#define DEBUG_LB 1
int _offset = 0;

inline void rewrite_ips(WritablePacket* q, IPPair pair) {
    assert(q->network_header());
    uint16_t *x = reinterpret_cast<uint16_t *>(&q->ip_header()->ip_src);
    uint32_t old_hw = (uint32_t) x[0] + x[1] + x[2] + x[3];
    old_hw += (old_hw >> 16);

    memcpy(x, &pair, 8);

    uint32_t new_hw = (uint32_t) x[0] + x[1] + x[2] + x[3];
    new_hw += (new_hw >> 16);
    click_ip *iph = q->ip_header();
    click_update_in_cksum(&iph->ip_sum, old_hw, new_hw);
    click_update_in_cksum(&q->tcp_header()->th_sum, old_hw, new_hw);

}

FlowIPLoadBalancer::FlowIPLoadBalancer() : _last(0){

};

FlowIPLoadBalancer::~FlowIPLoadBalancer() {

}

int
FlowIPLoadBalancer::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
               .read_all("DST",Args::mandatory | Args::positional,DefaultArg<Vector<IPAddress>>(),_dsts)
               .read("SIP",_sip)
               .complete() < 0)
        return -1;
    click_chatter("%p{element} has %d routes",this,_dsts.size());
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
        flowdata->src = _sip;
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
        click_chatter("Adding entry %s %d",entry.chosen_server.unparse().c_str(),entry.port);
#endif
    }

    EXECUTE_FOR_EACH_PACKET([flowdata](Packet*p) -> Packet*{
        WritablePacket* q=p->uniqueify();
        rewrite_ips(q, *flowdata);
        q->set_dst_ip_anno(flowdata->dst);
        return q;
    }, batch);

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
        LBHashtable::ptr ptr = _lb->_map.find(entry);
        if (!ptr) {
            checked_output_push_batch(0, batch);
#if DEBUG_LB
            click_chatter("Could not find %s %d",IPAddress(ip->ip_src).unparse().c_str(),th->th_dport);
#endif
            //assert(false);
            return;
        } else{
#if DEBUG_LB
            click_chatter("Found entry %s %d : %s -> %s",entry.chosen_server.unparse().c_str(),entry.port,ptr->src.unparse().c_str(),ptr->dst.unparse().c_str());
#endif
        }
        *flowdata = *ptr;
    } else {
#if DEBUG_LB
        click_chatter("Saved entry %s -> %s",flowdata->src.unparse().c_str(),flowdata->dst.unparse().c_str());
#endif
    }

    EXECUTE_FOR_EACH_PACKET([flowdata](Packet*p) -> Packet*{
            WritablePacket* q=p->uniqueify();
            rewrite_ips(q, *flowdata);
            return q;
        }, batch);

    checked_output_push_batch(0, batch);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(FlowIPLoadBalancerReverse)
ELEMENT_MT_SAFE(FlowIPLoadBalancerReverse)
EXPORT_ELEMENT(FlowIPLoadBalancer)
ELEMENT_MT_SAFE(FlowIPLoadBalancer)
