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
#include <list>

CLICK_DECLS

#ifdef HAVE_BATCH

#define BATCH_MAX_PULL 256
class PushToPushBatchVisitor;
class BatchElement : public Element { public:
	BatchElement();

	~BatchElement();

	virtual bool need_batch() const;

	virtual PacketBatch* simple_action_batch(PacketBatch* batch) {
        click_chatter("Warning in %s : simple_action_batch should be implemented."
         " This element is useless, batch will be returned untouched.",name().c_str());
        return batch;
	}

	virtual void push_batch(int port, PacketBatch* head) {
		head = simple_action_batch(head);
		if (head)
			output(port).push_batch(head);
	}

	virtual PacketBatch* pull_batch(int port, unsigned max) {
	    PacketBatch* head = input(port).pull_batch(max);
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

	virtual void push(int port,Packet* p) { //May still be extended
		if (inflow.get()) {
				if (current_batch.get() == NULL) {
					current_batch.set(PacketBatch::make_from_packet(p));
				} else {
					current_batch.get()->append_packet(p);
				}
		}
		else if (need_batch()) {
		    click_chatter("BUG : lonely packet sent to an element which needs batch !");
			push_batch(port,PacketBatch::make_from_packet(p));
		} else {
		    Element::push(port,p);
		}
	};

	inline void checked_output_push_batch(int port, PacketBatch* batch) {
		 if ((unsigned) port < (unsigned) noutputs())
			 output(port).push_batch(batch);
		 else
			 batch->kill();
	}

	class BatchPort : public Port {
	public :

		~BatchPort() {

		}

		bool output_supports_batch;
		std::list<BatchElement*> downstream_batches;

		std::list<BatchElement*>& getDownstreamBatches() {
			return downstream_batches;
		}

		void push_batch(PacketBatch* head) const;
		inline void assign(bool isoutput, Element *e, int port);
		void bind_batchelement();
		friend class BatchElement;
	};

	class PullBatchPort : public Port {
	    public :
	    PacketBatch* pull_batch(unsigned max) const;
		inline Packet* pull() const {
			click_chatter("WARNING : Using pull function inside a batch-compatible element. This will likely create errors, please change the element to use pull_batch instead !");
			return _e->pull(_port);
		}
	    void bind_batchelement();
	};

	inline const BatchPort&
	output(int port)
	{
		return static_cast<const BatchPort&>(static_cast<BatchElement::BatchPort*>(_ports[1])[port]);
	}

	inline const PullBatchPort&
	input(int port)
    {
        return static_cast<const PullBatchPort&>(_ports[0][port]);
    }

	void upgrade_ports();
	void check_unbatch();

	protected :
	bool receives_batch;
	bool ports_upgraded;
	friend class PushToPushBatchVisitor;
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
