// -*- c-basic-offset: 4; related-file-name: "../include/click/flowelement.hh" -*-
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

BatchElement::BatchElement() : current_batch(NULL),inflow(0)
{

}

BatchElement::~BatchElement() {
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
		for (auto it = downstream_batches.begin(); it!= downstream_batches.end(); it++)
			(*it)->start_batch();

		while (head != NULL) {
			Packet* next = head->next();
			head->set_next(NULL);
			Element::Port::push(head);
			head = next;
		}

		for (auto it = downstream_batches.begin(); it!= downstream_batches.end(); it++)
			(*it)->end_batch();

	}
}

/**
 * Assign an element to this port
 */
inline void BatchElement::BatchPort::assign(bool isoutput, Element *e, int port) {
	output_supports_batch = dynamic_cast<BatchElement*>(e) != NULL;
	Element::Port::assign(isoutput,e,port);
	#if HAVE_BOUND_PORT_TRANSFER && HAVE_BATCH
		if (output_supports_batch) {
			void (BatchElement::*flow_pusher)(int, PacketBatch *) = &BatchElement::push_batch;
			_bound.push_batch = (void (*)(BatchElement *, int, PacketBatch *)) (static_cast<BatchElement*>(e)->*flow_pusher);
		}
	#endif
}


/**
 * RouterVisitor finding all reachable BatchElement from another given BatchElement
 */
class PushToPushBatchVisitor : public RouterVisitor { public:

	PushToPushBatchVisitor(std::list<BatchElement*> &list) : _list(list) {

	}

	bool visit(Element *e, bool isoutput, int port,
			Element *from_e, int from_port, int distance) {
		BatchElement* batch_e = dynamic_cast<BatchElement*>(e);
		if (batch_e != NULL) {
			//We add this only if it's not reconstruction for just one element...
			bool makesense = true;

			//Check if all elements downstream support batch
			for (int i = 0; i < batch_e->nports(isoutput); i++) {
				if (dynamic_cast<BatchElement*>(batch_e->port(isoutput, i).element()) == NULL) {
					makesense = false;
					break;
				}
			}
			if (makesense) {
				_list.push_back(batch_e);
			}
			return false;
		};
		return true;
	}

	std::list<BatchElement*> &_list;

};

/**
 * Upgrade the ports of this element to support communication between batch-compatible elements
 */
void BatchElement::upgrade_ports() {
	int io = 1;
	bool is_inline =
			(_ports[io] >= _inline_ports && _ports[io] <= _inline_ports + INLINE_PORTS);
    click_chatter("Upgrading %s (%p)",name().c_str(),this);
	BatchPort* newports = new BatchPort[_nports[io]];
	for (int i = 0; i < _nports[io]; i++) {
		newports[i].assign(io,_ports[io][i].element(),_ports[io][i].port());
	}
	if (!is_inline)
		delete[] _ports[io];
	_ports[io] = newports;
}

/**
 * Check if input and unput elements support batching. If they don't, ports
 * 	will be set in support mode to unbatch/rebatch on batch request
 */
void BatchElement::check_unbatch() {
	for (int i = 0; i < noutputs(); i++) {
		if (!output(i).output_supports_batch) {
			click_chatter("Warning ! %s (%p) is not compatible with batch. Performance will be slightly worst.",output(i).element()->name().c_str(), output(i).element());
			PushToPushBatchVisitor v(static_cast<BatchElement::BatchPort>(output(i)).getDownstreamBatches());
			router()->visit(this,1,i,&v);
		}
	}
}
#endif


CLICK_ENDDECLS
