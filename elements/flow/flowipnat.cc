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

    /*
     * Get "touching" threads. That is threads passing by us and touching
     * our state.
     * NATReverse takes care of telling that it will touch our hashtable
     * therefore touching is actually the passing threads for both directions
     */
    Bitvector touching = get_passing_threads();

    /**
     * If only one thread touch this element, disable MT-safeness.
     */
    if (touching.weight() <= 1) {
        _map.disable_mt();
    }

    /**
     * Get passing threads, that is the threads that will call push_batch
     */
    Bitvector passing = get_passing_threads(false);
    if (passing.weight() == 0) {
        return errh->warning("No thread passing by, element will not work if it's not indeed idle");
    }

    /**
     * Now allocate ports for each thread
     */
    int total_ports = 65535 - 1024;
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
            s.available_ports.push_back(new PortRef(port));
        }
        n++;
    }
    return 0;
}

bool FlowIPNAT::new_flow(NATEntryIN* fcb, Packet* p) {
    IPAddress osip = IPAddress(p->ip_header()->ip_src);
    uint16_t oport = p->tcp_header()->th_sport;
    PortRef* ref = 0;
    if (_state->available_ports.empty()) {
        for (int i = 0; i < _state->to_release.size(); i++) {
            if (_state->to_release[i]->ref.value() == 0) {
                ref = _state->to_release[i];
            }
        }
        if (!ref) {
            click_chatter("ERROR %p{element} : no more ports available !",this);
            return false;
        }
    } else {
        ref = _state->available_ports.front();
        _state->available_ports.pop_front();
    }
    fcb->ref = ref;
    NATEntryOUT out = {IPPort(osip,oport),ref};
    _map.find_insert(ref->port, out);
    return true;
}

void FlowIPNAT::release_flow(NATEntryIN* fcb) {
    if (fcb->ref->ref.dec_and_test()) {
        _state->available_ports.push_back(fcb->ref);
    } else {
        _state->to_release.push_back(fcb->ref);
    }
}

void FlowIPNAT::push_batch(int port, NATEntryIN* flowdata, PacketBatch* batch) {
    auto fnt = [this,flowdata](Packet*p) -> Packet*{
        WritablePacket* q=p->uniqueify();
        q->rewrite_ipport(_sip, flowdata->ref->port, true);
        if ((q->tcp_header()->th_flags & TH_RST) || ((q->tcp_header()->th_flags & TH_FIN) && (q->tcp_header()->th_flags | TH_ACK))) {
            close_flow();
            release_flow(flowdata);
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

bool FlowIPNATReverse::new_flow(NATEntryOUT* fcb, Packet* p) {
    auto th = p->tcp_header();

    bool found = _in->_map.find_remove(th->th_dport,*fcb);
    return found;
}

void FlowIPNATReverse::release_flow(NATEntryOUT* fcb) {
    --fcb->ref->ref;
}

void FlowIPNATReverse::push_batch(int port, NATEntryOUT* flowdata, PacketBatch* batch) {
    //_state->port_epoch[*flowdata] = epoch;
    auto fnt = [this,flowdata](Packet*p) -> Packet*{
        WritablePacket* q=p->uniqueify();
        q->rewrite_ipport(flowdata->map.ip, flowdata->map.port, 1);
        q->set_dst_ip_anno(flowdata->map.ip);
        return q;
        if ((q->tcp_header()->th_flags & TH_RST) || ((q->tcp_header()->th_flags & TH_FIN) && (q->tcp_header()->th_flags | TH_ACK))) {
            close_flow();
            release_flow(flowdata);
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
