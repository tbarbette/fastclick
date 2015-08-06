// -*- c-basic-offset: 4; related-file-name: "../include/click/batchelement.hh" -*-
/*
 * batchelement.{cc,hh}
 *
 * SubClass of Element used for elements supporting batching.
 */
#include <click/config.h>
#include <click/glue.hh>
#include <click/batchelement.hh>
#include <click/routervisitor.hh>

CLICK_DECLS

#ifdef HAVE_BATCH

bool BatchElement::need_batch() const {
    return false;
}

BatchElement::BatchElement() : current_batch(NULL),inflow(0),receives_batch(false),ports_upgraded(false)
{

}

BatchElement::~BatchElement() {
	if (_ports[1] && ports_upgraded)
		delete[] static_cast<BatchPort*>(_ports[1]);
	_ports[1] = 0;
}

/**
 * Push a batch through this port
 */
void BatchElement::BatchPort::push_batch(PacketBatch* batch) const {
	Packet* head = batch;
	if (likely(output_supports_batch)) {
#if HAVE_BOUND_PORT_TRANSFER
		_bound.push_batch(static_cast<BatchElement*>(_e),_port,batch);
#else
		static_cast<BatchElement*>(_e)->push_batch(_port,batch);
#endif
	} else {
		for (std::list<BatchElement*>::const_iterator it = downstream_batches.begin(); it!= downstream_batches.end(); it++)
			(*it)->start_batch();

		while (head != NULL) {
			Packet* next = head->next();
			head->set_next(NULL);
			Element::Port::push(head);
			head = next;
		}

		for (std::list<BatchElement*>::const_iterator it = downstream_batches.begin(); it!= downstream_batches.end(); it++)
			(*it)->end_batch();
	}
}

/**
 * Assign an element to this port
 */
inline void BatchElement::BatchPort::assign(bool isoutput, Element *e, int port) {
	output_supports_batch = dynamic_cast<BatchElement*>(e) != NULL;
	Element::Port::assign(isoutput,e,port);
	bind_batchelement();
}

void BatchElement::BatchPort::bind_batchelement() {
	if (dynamic_cast<BatchElement*>(element()) != NULL) {
		dynamic_cast<BatchElement*>(element())->receives_batch = true;
#if HAVE_BOUND_PORT_TRANSFER && HAVE_BATCH
		void (BatchElement::*flow_pusher)(int, PacketBatch *) = &BatchElement::push_batch;
		_bound.push_batch = (void (*)(BatchElement *, int, PacketBatch *)) (dynamic_cast<BatchElement*>(element())->*flow_pusher);
#endif
	}
}

/**
 * Assign an element to this port
 */
void BatchElement::PullBatchPort::bind_batchelement() {
    #if HAVE_BOUND_PORT_TRANSFER && HAVE_BATCH
    if (dynamic_cast<BatchElement*>(element()) != NULL) {
        PacketBatch *(BatchElement::*flow_pull)(int,unsigned) = &BatchElement::pull_batch;
        _bound.pull_batch = (PacketBatch * (*)(BatchElement *, int, unsigned)) (dynamic_cast<BatchElement*>(element())->*flow_pull);
    }
    #endif
}

PacketBatch* BatchElement::PullBatchPort::pull_batch(unsigned max) const {
    PacketBatch* batch = NULL;
    if (likely(dynamic_cast<BatchElement*>(_e) != NULL)) {
#if HAVE_BOUND_PORT_TRANSFER
        batch = _bound.pull_batch(static_cast<BatchElement*>(_e),_port, max);
#else
        batch = static_cast<BatchElement*>(_e)->pull_batch(_port, max);
#endif
        return batch;
    } else {
        MAKE_BATCH(Element::Port::pull(),batch,max);
        return batch;
    }
}

/**
 * RouterVisitor finding all reachable BatchElement from another given BatchElement
 */
class PushToPushBatchVisitor : public RouterVisitor { public:

	PushToPushBatchVisitor(std::list<BatchElement*> *list) : _list(list) {

	}

	bool visit(Element *e, bool isoutput, int,
			Element *, int, int) {
		BatchElement* batch_e = dynamic_cast<BatchElement*>(e);
		if (batch_e != NULL) {
			/*We add this only if it's not reconstruction for just one element
		        or if the elements only supports batches*/

			bool reconstruct_batch = true;

			if (!batch_e->need_batch() && !batch_e->receives_batch) {
                //Check if all elements downstream support batch; if not we do not reconstruct. Force reconstruct if this element can receive batch
                for (int i = 0; i < batch_e->nports(isoutput); i++) {
                    if (dynamic_cast<BatchElement*>(batch_e->port(isoutput, i).element()) == NULL) {
                        reconstruct_batch = false;
                        break;
                    }
                }
			}

			if (reconstruct_batch) {
				click_chatter("Add %s",batch_e->name().c_str());
				_list->push_back(batch_e);
				return false;
			} else {
			    return false;
			}

		};
		return true;
	}

	std::list<BatchElement*> *_list;

};

/**
 * Upgrade the ports of this element to support communication between batch-compatible elements
 */
void BatchElement::upgrade_ports() {
    for (int i = 0; i < _nports[0]; i++) {
        ((PullBatchPort&)_ports[0][i]).bind_batchelement();
    }
	int io = 1;
	bool is_inline =
			(_ports[io] >= _inline_ports && _ports[io] <= _inline_ports + INLINE_PORTS);
	BatchPort* newports = new BatchPort[_nports[io]];
	for (int i = 0; i < _nports[io]; i++) {
		newports[i].assign(io,_ports[io][i].element(),_ports[io][i].port());
	}
	if (!is_inline)
		delete[] _ports[io];
	_ports[io] = newports;
	ports_upgraded = true;
}

/**
 * Check if input and output elements support batching. If they don't, ports
 * 	will be set in support mode to unbatch/rebatch on batch request
 */
void BatchElement::check_unbatch() {
	for (int i = 0; i < noutputs(); i++) {
		if (output_is_push(i) && !output(i).output_supports_batch) {
			click_chatter("Warning ! %s->%s is not compatible with batch. Packets will be unbatched and that will reduce performances.",output(i).element()->name().c_str(),name().c_str());
			BatchPort* port = &(static_cast<BatchElement::BatchPort*>(_ports[1])[i]);
			PushToPushBatchVisitor v(&port[i].downstream_batches);
			router()->visit(this,1,i,&v);
		}
	}
}
#endif


CLICK_ENDDECLS
