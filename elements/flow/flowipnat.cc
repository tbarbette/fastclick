/*
 * FlowIPNAT.{cc,hh}
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/flow.hh>
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include "flowipnat.hh"

CLICK_DECLS


#define DEBUG_NAT 0

FlowIPNAT::FlowIPNAT() : _sip(){

};

FlowIPNAT::~FlowIPNAT() {

}

int
FlowIPNAT::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
               .read("SIP",_sip)
               .complete() < 0)
        return -1;

    return 0;
}


int FlowIPNAT::initialize(ErrorHandler *errh) {

    return 0;
}

void FlowIPNAT::push_batch(int port, IPPair* flowdata, PacketBatch* batch) {
    if (flowdata->src == IPAddress(0)) {
#if DEBUG_NAT
        click_chatter("New flow");
#endif
        auto ip = batch->ip_header();
        IPAddress osip = IPAddress(ip->ip_src);
        IPAddress odip = IPAddress(ip->ip_dst);
        auto th = batch->tcp_header();
        NATEntry entry = NATEntry(flowdata->dst, th->th_sport);
        _map.find_insert(entry, IPPair(odip,osip));
#if DEBUG_NAT
        click_chatter("Adding entry %s %d [%d]",entry.dst.unparse().c_str(),entry.port);
#endif
        fcb_acquire();
    }

    EXECUTE_FOR_EACH_PACKET([flowdata](Packet*p) -> Packet*{
        WritablePacket* q=p->uniqueify();
        q->rewrite_ips(*flowdata);
        q->set_dst_ip_anno(flowdata->dst);
        return q;
    }, batch);

    checked_output_push_batch(0, batch);
}

FlowIPNATReverse::FlowIPNATReverse() {

};

FlowIPNATReverse::~FlowIPNATReverse() {

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

void FlowIPNATReverse::push_batch(int port, IPPair* flowdata, PacketBatch* batch) {
    if (flowdata->src == IPAddress(0)) {
        auto ip = batch->ip_header();
        auto th = batch->tcp_header();
        NATEntry entry = NATEntry(ip->ip_src, th->th_dport);
#if IPNATR_MP
        NATHashtable::ptr ptr = _in->_map.find(entry);
#else
        NATHashtable::iterator ptr = _in->_map.find(entry);
#endif
        if (!ptr) {

#if DEBUG_NAT
            click_chatter("Could not find %s %d",IPAddress(ip->ip_src).unparse().c_str(),th->th_dport);
#endif
            //assert(false);
            //checked_output_push_batch(0, batch);
            batch->fast_kill();
            return;
        } else{
#if DEBUG_NAT
            click_chatter("Found entry %s %d : %s -> %s",entry.chosen_server.unparse().c_str(),entry.port,ptr->src.unparse().c_str(),ptr->dst.unparse().c_str());
#endif
        }
#if IPNATR_MP
        *flowdata = *ptr;
#else
        *flowdata = ptr.value();
#endif
        fcb_acquire();
    }

    EXECUTE_FOR_EACH_PACKET([flowdata](Packet*p) -> Packet*{
            WritablePacket* q=p->uniqueify();
            q->rewrite_ips(*flowdata);
            q->set_dst_ip_anno(flowdata->dst);
            return q;
        }, batch);

    checked_output_push_batch(0, batch);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(FlowIPNATReverse)
ELEMENT_MT_SAFE(FlowIPNATReverse)
EXPORT_ELEMENT(FlowIPNAT)
ELEMENT_MT_SAFE(FlowIPNAT)
