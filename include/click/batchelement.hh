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

#define BATCH_MAX_PULL 1024

class BatchElement : public Element { public:
	BatchElement();

	~BatchElement();

	virtual const bool need_batch() const;

	virtual PacketBatch* simple_action_batch(PacketBatch* batch) {
        click_chatter("Warning in %s : simple_action_batch should be implemented."
         " This element is useless, batch will be returned untouched.",name().c_str());
        return batch;
	}


	PacketBatch* pull_myself_batch(int port) {
	    PacketBatch* head = PacketBatch::start_head(pull(port));
	    Packet* last = head;
	    if (head != NULL) {
	        unsigned int count = 1;
	        do {
	            Packet* current = pull(port);
	            if (current == NULL)
	                break;
	            last->set_next(current);
	            last = current;
	            count++;
	        }
	        while (count < BATCH_MAX_PULL);

	        return head->make_tail(last,count);
	    }
	    return NULL;
	}

	virtual void push_batch(int port, PacketBatch* head) {
		head = simple_action_batch(head);
		if (head)
			output(port).push_batch(head);
	}

	virtual PacketBatch* pull_batch(int port) {
	    PacketBatch* head = input(port).pull_batch();
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

	void push(int port,Packet* p) {
		if (inflow.get()) {
				if (current_batch.get() == NULL) {
					current_batch.set(PacketBatch::make_from_packet(p));
				} else {
					current_batch.get()->append_packet(p);
				}
		}
		else
			push_batch(port,PacketBatch::make_from_packet(p));
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

		friend class BatchElement;

	};

	class PullBatchPort : public Port {
	    public :
	    PacketBatch* pull_batch() const;
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
