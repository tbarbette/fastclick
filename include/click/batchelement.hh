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

//#define BATCH_DEBUG 1

#define BATCH_MAX_PULL 256
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

	per_thread<PacketBatch*> current_batch;
	per_thread<bool> inflow; //Remember if we are currently rebuilding a batch

	inline void start_batch() {
		inflow.set(true);
	}

	inline void end_batch() {
		if (inflow.get() && current_batch.get()) {
			push_batch(0,current_batch.get());
			current_batch.set(0);
		}
		inflow.set(false);
	}

	void push(int port,Packet* p) final;

	virtual void push_packet(int port,Packet* p);

	inline void checked_output_push_batch(int port, PacketBatch* batch) {
		 if ((unsigned) port < (unsigned) noutputs())
			 output_push_batch(port,batch);
		 else
			 batch->fast_kill();
	}

	class PushBatchPort : public Port {
	public :

		~PushBatchPort() {

		}

		std::list<BatchElement*> downstream_batches;

		std::list<BatchElement*>& getDownstreamBatches() {
			return downstream_batches;
		}

		void push_batch(PacketBatch* head) const;
		void bind_batchelement();
		friend class BatchElement;
	};

	class PullBatchPort : public Port {
	    public :
	    PacketBatch* pull_batch(unsigned max) const;
		inline Packet* pull() const {
			BatchElement* be = dynamic_cast<BatchElement*>(_e);
			if (be) {
				PacketBatch* batch = be->pull_batch(_port,1);
				if (batch == 0)
					return 0;
				assert(batch->count() == 1);
				return batch;
			} else {
				return _e->pull(_port);
			}
		}
	    void bind_batchelement();
	};

	inline const PushBatchPort&
	output_batch(int port)
	{
		return static_cast<const PushBatchPort&>(static_cast<BatchElement::PushBatchPort*>(_ports[1])[port]);
	}

	inline const PullBatchPort&
	input_batch(int port)
    {
        return static_cast<const PullBatchPort&>(_ports[0][port]);
    }

	inline void
	output_push_batch(int port, PacketBatch* batch) {
		output_batch(port).push_batch(batch);
	}

	inline PacketBatch*
	input_pull_batch(int port, int max) {
		return input_batch(port).pull_batch(max);
	}

	enum batch_mode batch_mode() {
		return in_batch_mode;
	}

	void upgrade_ports();
	void bind_ports();

	protected :

	enum batch_mode in_batch_mode;
	bool receives_batch;
	bool ports_upgraded;

	/**
	 * Propagate a BATCH_MODE_YES upstream or downstream
	 */
	class BatchModePropagate : public RouterVisitor { public:
		bool _verbose;
		bool ispush;

		BatchModePropagate() : ispush(true) {
#if HAVE_VERBOSE_BATCH
			_verbose = true;
#else
			_verbose = false;
#endif
		}

		bool visit(Element *e, bool isoutput, int,
				Element *, int, int);
	};

	/**
	 * RouterVisitor finding all reachable batch-enabled element
	 */
	class PushToPushBatchVisitor : public RouterVisitor { public:

		PushToPushBatchVisitor(std::list<BatchElement*> *list);

		bool visit(Element *, bool, int,
				Element *, int, int);
		std::list<BatchElement*> *_list;
	};

	friend class Router;
};




#else
class BatchElement : public Element { public:
	inline void checked_output_push_batch(int port, PacketBatch* batch) {
		output(port).push(batch);
	}

	virtual void push_packet(int port,Packet* p);


	void push(int port,Packet* p) final {
		push_packet(port, p);
	};


};
#endif

CLICK_ENDDECLS
#endif
