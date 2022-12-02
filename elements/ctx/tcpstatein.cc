/*
 * tcpstatein.{cc,hh}
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include "tcpstatein.hh"
#include <click/flow/flow.hh>

CLICK_DECLS

TCPStateIN::TCPStateIN() : _map(65535),  _accept_nonsyn(true), _verbose(0) {

};

TCPStateIN::~TCPStateIN() {

}

int
TCPStateIN::configure(Vector<String> &conf, ErrorHandler *errh)
{
    Element* e;
    if (Args(conf, this, errh)
                .read_mp("RETURNNAME",e)
                .read_or_set("ACCEPT_NONSYN", _accept_nonsyn, true)
				.read_or_set("VERBOSE", _verbose, 0)
                .complete() < 0)
        return -1;

    _return = reinterpret_cast<TCPStateIN*>(e);

    return 0;
}


int TCPStateIN::initialize(ErrorHandler *errh) {

    _pool.static_initialize();
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

    TCPStateCommon* common;
    bool found = _return->_map.find_erase_clean(IPFlowID(p),[&common](TCPStateCommon* &c){common=c;},[this](TCPStateCommon* &c){ //This function is called under read lock
                if (c->use_count == 1) { //Inserter removed the reference without it being grabbed by other side
                    if (c->use_count.dec_and_test()) {
                        _pool.release(c);
                        //No established-- because it was not established
                    }
                    return true;
                }
                return false;
                });
	if (found) {
		if (_verbose)
			click_chatter("Found entry, map has %d entries!",_return->_map.size());

		fcb->common = common;
		fcb->fin_seen = false;
//we keep the reference from the table
//            ++fcb->common->use_count;
		if (fcb->common->use_count == 1) { //Connection was reset, we have the only ref
			if (fcb->common->use_count.dec_and_test())
				_pool.release(fcb->common);
			fcb->common = 0;
			return false;
		}
		_established ++;
		return true;
	}
    if (!_accept_nonsyn && !(p->tcp_header()->th_flags & TH_SYN)) {
        click_chatter("Flow does not start with a SYN...");
        return false;
    }
    if (_verbose)
        click_chatter("New entry (return map has %d entries, my map has %d entries)!",_return->_map.size(), _map.size());
    fcb->common = _pool.allocate();
    fcb->common->use_count = 2; //us and the table
    fcb->common->closing = false;
    fcb->fin_seen = false;

    _map.find_insert(IPFlowID(p).reverse(), fcb->common);
    if (_verbose)
        click_chatter("Map has %d entries", _map.size());
    return true;
}

void TCPStateIN::release_flow(TCPStateEntry* fcb) {
    if (_verbose)
        click_chatter("Release entry!");
    if (fcb->common)
    {
        if (fcb->common->use_count.dec_and_test()) {
		_established --;
            _pool.release(fcb->common);
            if (_verbose)
                click_chatter("Release FCB common!");
        }
        fcb->common = 0;
    }
}

void TCPStateIN::push_flow(int port, TCPStateEntry* flowdata, PacketBatch* batch) {
    auto fnt = [this,flowdata](Packet* p) -> Packet*{
        if (!flowdata->common) {
            //A packet arrived without a common but which is already seen. That's probably reusing of a connection, or a lost packet from a long time ago
            return 0;
        }

        if (unlikely(p->tcp_header()->th_flags & TH_RST)) { //RST, this side will never see any useful packet
            close_flow();
            release_flow(flowdata);
            return p;
        } else if (unlikely(p->tcp_header()->th_flags & TH_FIN)) {
            if (flowdata->fin_seen) { //Fin retransmit
            } else {

                flowdata->fin_seen = true; //First fin on this side
                if (flowdata->common->closing && p->tcp_header()->th_flags & TH_ACK) {
                    //This is the second fin from the other side
                    close_flow();
                    release_flow(flowdata);
                } else {
                    flowdata->common->closing = true; //This is the first fin, this side will send the final ACK
                }
            }
        } else if (unlikely(flowdata->common->closing && p->tcp_header()->th_flags & TH_ACK && flowdata->fin_seen)) {
            close_flow();
            release_flow(flowdata);
        }
        return p;
    };
    EXECUTE_FOR_EACH_PACKET_DROP_LIST(fnt, batch, drop);

    output_push_batch(0, batch);

    if (drop) {
        checked_output_push_batch(1, drop);
    }
}

enum {h_map_size, h_established};
String TCPStateIN::read_handler(Element* e, void* thunk) {
    TCPStateIN* tc = static_cast<TCPStateIN*>(e);

    switch ((intptr_t)thunk) {
        case h_map_size:
            return String(tc->_map.size());
        case h_established:
		return String(tc->_established);
    }
    return String("");
}

void TCPStateIN::add_handlers() {

    add_read_handler("map_size", TCPStateIN::read_handler, h_map_size);
    add_read_handler("established", TCPStateIN::read_handler, h_established);

}


void TCPStateIN::static_initialize() {
}


pool_allocator_mt<TCPStateCommon,false,16384> TCPStateIN::_pool;

CLICK_ENDDECLS
ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(TCPStateIN)
ELEMENT_MT_SAFE(TCPStateIN)
