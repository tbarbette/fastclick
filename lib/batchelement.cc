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

BatchElement::BatchElement() : current_batch(NULL),inflow(0),in_batch_mode(Element::BATCH_MODE_IFPOSSIBLE),receives_batch(false),ports_upgraded(false)
{

}

BatchElement::~BatchElement() {
	if (_ports[1] && ports_upgraded)
		delete[] static_cast<PushBatchPort*>(_ports[1]);
	_ports[1] = 0;
}

/**
 * Default push action for a batch element, will batch packets if inflow and
 */
void BatchElement::push(int port,Packet* p) { //May still be extended
	if (inflow.get()) {
		if (port != 0) {
			push_batch(port,PacketBatch::make_from_packet(p));
		} else {
			if (current_batch.get() == NULL) {
				current_batch.set(PacketBatch::make_from_packet(p));
			} else {
				current_batch.get()->append_packet(p);
			}
		}
	} else if (in_batch_mode == BATCH_MODE_YES) {
		push_batch(port,PacketBatch::make_from_packet(p));
	} else {
		push_packet(port,p);
	}
};

/**
 * Push a batch through this port
 */
void
BatchElement::PushBatchPort::push_batch(PacketBatch* batch) const {
	Packet* head = batch;
	if (likely(e_in_batch_mode)) {
#if HAVE_BOUND_PORT_TRANSFER
		_bound.push_batch(static_cast<BatchElement*>(_e),_port,batch);
#else
		static_cast<BatchElement*>(_e)->push_batch(_port,batch);
#endif
	} else {
		for (std::list<BatchElement*>::const_iterator it = downstream_batches.begin(); it!= downstream_batches.end(); it++) {
			(*it)->start_batch();
		}
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

PacketBatch*
BatchElement::PullBatchPort::pull_batch(unsigned max) const {
    PacketBatch* batch = NULL;
    if (likely(e_in_batch_mode)) {
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

void BatchElement::PushBatchPort::bind_batchelement() {
	BatchElement* be;
	if ((be = dynamic_cast<BatchElement*>(element())) != NULL) {
		e_in_batch_mode = (be->in_batch_mode == BATCH_MODE_YES);
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
	BatchElement* be;
    if ((be = dynamic_cast<BatchElement*>(element())) != NULL) {
        e_in_batch_mode = (be->in_batch_mode == BATCH_MODE_YES);
#if HAVE_BOUND_PORT_TRANSFER && HAVE_BATCH
        PacketBatch *(BatchElement::*flow_pull)(int,unsigned) = &BatchElement::pull_batch;
        _bound.pull_batch = (PacketBatch * (*)(BatchElement *, int, unsigned)) (dynamic_cast<BatchElement*>(element())->*flow_pull);
#endif
    }
}

bool BatchElement::BatchModePropagate::visit(Element *e, bool isoutput, int port,
		Element *from, int from_port, int) {
	//Do not continue if we change from pull to push
	if ((ispush && !from->output_is_push(from_port)) || (!ispush && !from->input_is_pull(from_port))) return false;

	if (e->batch_mode() > Element::BATCH_MODE_NO) {
		BatchElement* batch_e = dynamic_cast<BatchElement*>(e);
		batch_e->in_batch_mode = Element::BATCH_MODE_YES;
		batch_e->upgrade_ports();
#if BATCH_DEBUG
		if (_verbose)
			click_chatter("%s is now in batch mode",e->name().c_str());
#endif
		return true;
	}
	BatchElement* from_batch_e = dynamic_cast<BatchElement*>(from);
	assert(from_batch_e);
	if (!from_batch_e->ports_upgraded)
		from_batch_e->upgrade_ports();

	if (_verbose) {
		if (ispush)
			click_chatter("Warning ! Push %s->%s is not compatible with batch. "
					"Packets will be unbatched and that will reduce performances.",
					from->name().c_str(),e->name().c_str());
		else
			click_chatter("Warning ! Pull %s<-%s is not compatible with batch. "
					"Batching will be disabled and that will reduce performances.",
							e->name().c_str(),from->name().c_str());
	}

	//If this is push, we try to create a re-batching bridge
	if (ispush) {
		PushBatchPort* port = &(static_cast<BatchElement::PushBatchPort*>(from_batch_e->_ports[1])[from_port]);
		PushToPushBatchVisitor v(&port->downstream_batches);
		e->router()->visit(e,1,-1,&v);
	}
	return false;
}

/**
 * RouterVisitor finding all reachable batch-enabled element
 */

	BatchElement::PushToPushBatchVisitor::PushToPushBatchVisitor(std::list<BatchElement*> *list) : _list(list) {

	}

	bool BatchElement::PushToPushBatchVisitor::visit(Element *e, bool, int,
			Element *, int, int) {
		BatchElement* batch_e = dynamic_cast<BatchElement*>(e);
		if (e->batch_mode() == BATCH_MODE_IFPOSSIBLE) {
			batch_e->in_batch_mode = BATCH_MODE_YES;
			_list->push_back(batch_e);
			batch_e->receives_batch = true;
			return false;
		};
		if (batch_e != 0)
			batch_e->receives_batch = false;
		return true;
	}

/**
 * Upgrade the ports of this element to support communication between batch-compatible elements
 */
void BatchElement::upgrade_ports() {
	if (in_batch_mode != BATCH_MODE_YES || ports_upgraded)
		return;
#if HAVE_VERBOSE_BATCH && BATCH_DEBUG
	click_chatter("Upgrading ports of %s",name().c_str());
#endif

	PushBatchPort* newports = new PushBatchPort[_nports[1]];
	for (int i = 0; i < _nports[1]; i++) {
		Port* p =  &_ports[1][i];
		if (output_is_push(i)) {
			newports[i].assign(1,p->element(),p->port());
		}
	}
	bool is_inline =
			(_ports[1] >= _inline_ports && _ports[1] <= _inline_ports + INLINE_PORTS);
	if (!is_inline)
		delete[] _ports[1];
	_ports[1] = newports;
	ports_upgraded = true;
}

void BatchElement::bind_ports() {
	for (int i = 0; i < _nports[0]; i++) {
		Port* p =  &_ports[0][i];
		if (input_is_pull(i)) {
			static_cast<PullBatchPort*>(p)->bind_batchelement();
		}
	}

	for (int i = 0; i < _nports[1]; i++) {
		PushBatchPort* p =  static_cast<BatchElement::PushBatchPort*>(_ports[1]);
		if (output_is_push(i)) {
			p[i].bind_batchelement();
		}
	}
}
#endif

void BatchElement::push_packet(int port, Packet* p) {
	p = simple_action(p);
	if (p)
		checked_output_push(port,p);
}

CLICK_ENDDECLS
