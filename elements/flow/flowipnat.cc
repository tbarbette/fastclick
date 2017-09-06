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

#define NAT_FLOW_TIMEOUT 60 * 1000

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
    Bitvector touching = get_passing_threads();

    /*NATReverse takes care of telling that it will touch our hashtable
     * therefore touching is actually the passing threads for both directions
     */

    if (touching.weight() <= 1) {
        _map.disable_mt();
    }

    Bitvector passing = get_passing_threads(false);
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
        int min_port = 1024 + (n*ports_per_thread);
        int max_port = min_port + ports_per_thread;
        s.available_ports.resize(ports_per_thread);
        for (int port = min_port; port < max_port; port++) {
            s.available_ports.push_back(port);
        }
        n++;
    }
    return 0;
}

void FlowIPNAT::push_batch(int port, uint16_t* flowdata, PacketBatch* batch) {
    if (*flowdata == 0) {
        IPAddress osip = IPAddress(batch->first()->ip_header()->ip_src);
        uint16_t oport = batch->tcp_header()->th_sport;
        uint16_t port = _state->available_ports.front();
        _state->available_ports.pop_front();
        _map.find_insert(port, IPPort(osip,oport));
        fcb_acquire_timeout(NAT_FLOW_TIMEOUT);
    }

    //_state->port_epoch[*flowdata] = epoch;
    auto fnt = [this,flowdata](Packet*p) -> Packet*{
        WritablePacket* q=p->uniqueify();
        q->rewrite_ipport(_sip, *flowdata, true);
        if ((q->tcp_header()->th_flags & TH_RST) || ((q->tcp_header()->th_flags & TH_FIN) && (q->tcp_header()->th_flags | TH_ACK))) {
            fcb_release_timeout();
        }
        return q;
    };
    EXECUTE_FOR_EACH_PACKET(fnt, batch);

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

void FlowIPNATReverse::push_batch(int port, IPPort* flowdata, PacketBatch* batch) {
    if (flowdata->ip == IPAddress(0)) {
        auto ip = batch->ip_header();
        auto th = batch->tcp_header();

        bool found = _in->_map.find_remove(th->th_dport,*flowdata);
        if (!found) {
            batch->fast_kill();
            return;
        } else {
#if DEBUG_LB
            click_chatter("Found entry %s %d : %s -> %s",entry.chosen_server.unparse().c_str(),entry.port,ptr->pair.src.unparse().c_str(),ptr->pair.dst.unparse().c_str());
#endif
            fcb_acquire_timeout(NAT_FLOW_TIMEOUT);
	}
    }

    //_state->port_epoch[*flowdata] = epoch;
    auto fnt = [this,flowdata](Packet*p) -> Packet*{
        WritablePacket* q=p->uniqueify();
        q->rewrite_ipport(flowdata->ip, flowdata->port, 1);
        q->set_dst_ip_anno(flowdata->ip);
        return q;
        if ((q->tcp_header()->th_flags & TH_RST) || ((q->tcp_header()->th_flags & TH_FIN) && (q->tcp_header()->th_flags | TH_ACK))) {
            fcb_release_timeout();
        }
    };
    EXECUTE_FOR_EACH_PACKET(fnt, batch);

    checked_output_push_batch(0, batch);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(FlowIPNATReverse)
ELEMENT_MT_SAFE(FlowIPNATReverse)
EXPORT_ELEMENT(FlowIPNAT)
ELEMENT_MT_SAFE(FlowIPNAT)
