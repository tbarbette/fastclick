/*
 * TCPStateIN.{cc,hh}
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/flow.hh>
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include "tcpstatein.hh"

CLICK_DECLS

#define _verbose false

TCPStateIN::TCPStateIN() : _map(65535),  _accept_nonsyn(true) {

};

TCPStateIN::~TCPStateIN() {

}

int
TCPStateIN::configure(Vector<String> &conf, ErrorHandler *errh)
{
    Element* e;
    if (Args(conf, this, errh)
                .read_m("RETURNNAME",e)
                .read("ACCEPT_NONSYN", _accept_nonsyn)
                .complete() < 0)
        return -1;

    _return = reinterpret_cast<TCPStateIN*>(e);

    return 0;
}


int TCPStateIN::initialize(ErrorHandler *errh) {

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

    return 0;
}

bool TCPStateIN::new_flow(TCPStateEntry* fcb, Packet* p) {
    
    TCPStateCommon* ref;
        bool found = _return->_map.find_remove(IPFlowID(p),ref);
        if (found) {
            if (_verbose)
                click_chatter("Found entry!");
            auto th = p->tcp_header();
            fcb->ref = ref;
            fcb->fin_seen = false;
            ++fcb->ref->ref;
            if (fcb->ref->ref == 1) { //Connection was reset
                --fcb->ref->ref;
                return false;
            }
            return true;
        }
    if (!_accept_nonsyn && !p->tcp_header()->th_flags & TH_SYN) {
        click_chatter("Flow does not start with a SYN...");
        return false;
    }
    if (_verbose)
        click_chatter("New entry!");
    fcb->ref = _pool.allocate();
    fcb->ref->ref = 1;
    fcb->ref->closing = false;
    fcb->fin_seen = false;

    _map.find_insert(IPFlowID(p).reverse(), fcb->ref);
    return true;
}

void TCPStateIN::release_flow(TCPStateEntry* fcb) {
    if (_verbose)
        click_chatter("Release entry!");
    if (fcb->ref)
    {
        if (fcb->ref->ref.dec_and_test()) {
            _pool.release(fcb->ref);
            if (_verbose)
                click_chatter("Release FCB!");
        }
        fcb->ref = 0;
    }
}

void TCPStateIN::push_batch(int port, TCPStateEntry* flowdata, PacketBatch* batch) {
    auto fnt = [this,flowdata](Packet* p) -> Packet*{
        if (!flowdata->ref) {
            return 0;
        }

        if (unlikely(p->tcp_header()->th_flags & TH_RST)) {
            close_flow();
            release_flow(flowdata);
            return p;
        } else if (unlikely(p->tcp_header()->th_flags & TH_FIN)) {
            flowdata->fin_seen = true;
            if (flowdata->ref->closing && p->tcp_header()->th_flags & TH_ACK) {
                close_flow();
                release_flow(flowdata);
            } else {
                flowdata->ref->closing = true;
            }
        } else if (unlikely(flowdata->ref->closing && p->tcp_header()->th_flags & TH_ACK && flowdata->fin_seen)) {
            close_flow();
            release_flow(flowdata);
        }
        return p;
    };
    EXECUTE_FOR_EACH_PACKET_DROPPABLE(fnt, batch, (void));

    checked_output_push_batch(0, batch);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(TCPStateIN)
ELEMENT_MT_SAFE(TCPStateIN)
