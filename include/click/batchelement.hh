// -*- c-basic-offset: 4 -*-
#ifndef CLICK_BATCHELEMENT_HH
#define CLICK_BATCHELEMENT_HH
#include <click/glue.hh>
#include <click/vector.hh>
#include <click/string.hh>
#include <click/packet.hh>
#include <click/handler.hh>
#include <click/master.hh>
#include <click/element.hh>
#include <click/packet_anno.hh>
#include <click/multithread.hh>
#include <click/routervisitor.hh>
#include <list>

CLICK_DECLS

#ifdef HAVE_BATCH

class PushToPushBatchVisitor;
class BatchModePropagate;


class BatchElement : public Element { public:
	BatchElement();

	~BatchElement();

	virtual PacketBatch* simple_action_batch(PacketBatch* batch) {
        click_chatter("Warning in %s : simple_action_batch should be implemented."
         " This element is useless, batch will be returned untouched.",name().c_str());
        return batch;
	}

	virtual void push_batch(int port, PacketBatch* head) {
		head = simple_action_batch(head);
		if (head)
			output_push_batch(port,head);
	}

	virtual PacketBatch* pull_batch(int port, unsigned max) {
	    PacketBatch* head = input_pull_batch(port,max);
	    if (head) {
	        head = simple_action_batch(head);
	    }
	    return head;
	}

	inline void checked_output_push_batch(int port, PacketBatch* batch) {
		 if ((unsigned) port < (unsigned) noutputs())
			 output_push_batch(port,batch);
		 else
			 batch->fast_kill();
	}

	inline void
	output_push_batch(int port, PacketBatch* batch) {
		output(port).push_batch(batch);
	}

	inline PacketBatch*
	input_pull_batch(int port, int max) {
		return input(port).pull_batch(max);
	}


protected :

	/**
	 * Propagate a BATCH_MODE_YES upstream or downstream
	 */
	class BatchModePropagate : public RouterVisitor { public:
		bool ispush;

		BatchModePropagate() : ispush(true) {}

		bool visit(Element *e, bool isoutput, int,
				Element *, int, int);
	};

	/**
	 * RouterVisitor finding all reachable batch-enabled element
	 */
	class PushToPushBatchVisitor : public RouterVisitor { public:

		PushToPushBatchVisitor(Element* origin);

		bool visit(Element *, bool, int,
				Element *, int, int);
		Element* _origin;
	};

	friend class Router;
};

#else
class BatchElement : public Element { public:
	inline void checked_output_push_batch(int port, PacketBatch* batch) {
		output(port).push(batch);
	}
};
#endif

CLICK_ENDDECLS
#endif
