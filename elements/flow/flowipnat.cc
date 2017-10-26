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


#define DEBUG_NAT 0 //Do not debug
#define NAT_COLLIDE 1  //Avoid port collisions


FlowIPNAT::FlowIPNAT() : _sip() {

};

FlowIPNAT::~FlowIPNAT() {

}

int
FlowIPNAT::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
               .read_mp("SIP",_sip)
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
    Bitvector touching = get_passing_threads(true);

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
    int total_ports = 65536 - 1024;
    int ports_per_thread = total_ports / passing.weight();
    int n = 0;
    for (int i = 0; i < passing.size(); i++) {
        if (!passing[i])
            continue;
        state &s = _state.get_value_for_thread(i);
        int min_port = 1024 + (n*ports_per_thread);
        int max_port = min_port + ports_per_thread;
        s.available_ports.initialize(ports_per_thread);
        for (int port = min_port; port < max_port; port++) {
            s.available_ports.insert(new PortRef(htons(port)));
        }
        n++;
    }
    return 0;
}

PortRef* FlowIPNAT::pick_port() {
    int i = 0;
    PortRef* ref = 0;
    if (_state->available_ports.is_empty()) {
        click_chatter("%p{element} : Not even a used port available", this);
        return 0;
    } else {
        ref = _state->available_ports.extract();
#if NAT_COLLIDE
        while (ref->ref.value() > 0)
        {
            _state->available_ports.insert(ref);
            if (++i > _state->available_ports.count()) { //Full loop, stop here
                click_chatter("All ports are referenced...");
                return 0;
            }
            ref = _state->available_ports.extract();
        }
#else
        //Reinsert the reference at the back
        _state->available_ports.insert(ref);
#endif
    }
    return ref;
}

bool FlowIPNAT::new_flow(NATEntryIN* fcb, Packet* p) {
    if (!p->tcp_header()->th_flags & TH_SYN) {
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
    fcb->ref->ref = 1;
    fcb->ref->closing = false;
    fcb->fin_seen = false;
#endif
    //click_chatter("NEW osip %s osport %d",osip.unparse().c_str(),htons(oport));
    NATEntryOUT out = {IPPort(osip,oport),fcb->ref};
    _map.find_insert(fcb->ref->port, out);
    return true;
}

void FlowIPNAT::release_flow(NATEntryIN* fcb) {
//    click_chatter("Release %d",ntohs(fcb->ref->port));

#if NAT_COLLIDE
    if (fcb->ref)
    {
        --fcb->ref->ref;
//        click_chatter("fcb->ref is now %d",fcb->ref->ref);
//        assert((int32_t)fcb->ref->ref >= 0);
        if (!_state->available_ports.insert(fcb->ref)) {
            click_chatter("Double free");
            abort();
        }
    }
#else
    _state->available_ports.insert(fcb->ref);
#endif

    fcb->ref = 0;
}

void FlowIPNAT::push_batch(int port, NATEntryIN* flowdata, PacketBatch* batch) {
    auto fnt = [this,flowdata](Packet* p) -> Packet*{
        if (!flowdata->ref) {
            return 0;
        }
        WritablePacket* q=p->uniqueify();
        //click_chatter("Rewrite to %s %d",_sip.unparse().c_str(),htons(flowdata->ref->port));
        q->rewrite_ipport(_sip, flowdata->ref->port, 0, true);

#if NAT_COLLIDE
        if (unlikely(q->tcp_header()->th_flags & TH_RST)) {
            close_flow();
            release_flow(flowdata);
            return q;
        } else if (unlikely(q->tcp_header()->th_flags & TH_FIN)) {
            flowdata->fin_seen = true;
            if (flowdata->ref->closing && q->tcp_header()->th_flags & TH_ACK) {
                close_flow();
                release_flow(flowdata);
            } else {
                flowdata->ref->closing = true;
            }
        } else if (unlikely(flowdata->ref->closing && q->tcp_header()->th_flags & TH_ACK && flowdata->fin_seen)) {
            close_flow();
            release_flow(flowdata);
        }
#endif
        return q;
    };
    EXECUTE_FOR_EACH_PACKET_DROPPABLE(fnt, batch, (void));

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

    if (unlikely(!found)) {
        //click_chatter("FOUND %d, port %d, osip %s osport %d, RST %d, FIN %d, SYN %d",found,ntohs(th->th_dport),fcb->map.ip.unparse().c_str(),ntohs(fcb->map.port), th->th_flags & TH_RST, th->th_flags & TH_FIN, th->th_flags & TH_SYN);
        return false;
    }
    fcb->fin_seen = false;
    ++fcb->ref->ref;
    if (fcb->ref->ref == 1) { //Connection was reset
        --fcb->ref->ref;
        return false;
    }
    return true;
}

void FlowIPNATReverse::release_flow(NATEntryOUT* fcb) {
//    click_chatter("Release reverse %d",ntohs(fcb->ref->port));
#if NAT_COLLIDE
    --fcb->ref->ref;
//    click_chatter("fcb->ref is now %d",fcb->ref->ref);
//    assert((int32_t)fcb->ref->ref >= 0);
    fcb->ref = 0;
#endif
}

void FlowIPNATReverse::push_batch(int port, NATEntryOUT* flowdata, PacketBatch* batch) {
    //_state->port_epoch[*flowdata] = epoch;
    auto fnt = [this,flowdata](Packet*p) -> Packet*{
        //click_chatter("Rewrite to %s %d",flowdata->map.ip.unparse().c_str(),ntohs(flowdata->map.port));
        if (!flowdata->ref) {
            return 0;
        }
        WritablePacket* q=p->uniqueify();
        q->rewrite_ipport(flowdata->map.ip, flowdata->map.port, 1, true);
        q->set_dst_ip_anno(flowdata->map.ip);
#if NAT_COLLIDE
        if (unlikely(q->tcp_header()->th_flags & TH_RST)) {
            close_flow();
            release_flow(flowdata);
            return q;
        } else if ((q->tcp_header()->th_flags & TH_FIN)) {
            flowdata->fin_seen = true;
            if (flowdata->ref->closing && q->tcp_header()->th_flags & TH_ACK) {
                close_flow();
                release_flow(flowdata);
            } else {
                flowdata->ref->closing = true;
            }
        } else if (flowdata->ref->closing && q->tcp_header()->th_flags & TH_ACK && flowdata->fin_seen) {
            close_flow();
            release_flow(flowdata);
        }
#endif
        return q;
    };
    EXECUTE_FOR_EACH_PACKET_DROPPABLE(fnt, batch, (void));

    checked_output_push_batch(0, batch);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(FlowIPNATReverse)
ELEMENT_MT_SAFE(FlowIPNATReverse)
EXPORT_ELEMENT(FlowIPNAT)
ELEMENT_MT_SAFE(FlowIPNAT)
