// -*- c-basic-offset: 4 -*-
#ifndef CLICK_FLOWELEMENT_HH
#define CLICK_FLOWELEMENT_HH
#include <click/glue.hh>
#include <click/vector.hh>
#include <click/string.hh>
#include <click/batchelement.hh>
#include <click/flow.hh>
#include <click/routervisitor.hh>


CLICK_DECLS

#ifdef HAVE_FLOW

class FlowClassifier;



class FlowElement : public BatchElement {
public:
	FlowElement() {
		if (in_batch_mode < BATCH_MODE_NEEDED)
			in_batch_mode = BATCH_MODE_NEEDED;
	};
	~FlowElement() {};
	virtual FlowNode* get_table();
};


/**
 * Element that needs FCB space
 */
class VirtualFlowBufferElement : public FlowElement {
public:
	VirtualFlowBufferElement() :_flow_data_offset(-1), _classifier(0){
	}
	virtual const size_t flow_data_size() const = 0;
	virtual const int flow_data_index() const {
		return -1;
	}

	inline void set_flow_data_offset(int offset) {_flow_data_offset = offset; }
	inline int flow_data_offset() {return _flow_data_offset; }

	int configure_phase() const		{ return CONFIGURE_PHASE_DEFAULT + 5; }

protected:
	int _flow_data_offset;
	FlowClassifier* _classifier;
	friend class FlowBufferVisitor;

	FlowClassifier* upstream_classifier() {
		return _classifier;
	}
};

template<typename T> class FlowBufferElement : public VirtualFlowBufferElement {

public :

	FlowBufferElement() : VirtualFlowBufferElement() {

	}

	virtual int initialize(ErrorHandler *errh) {
		if (_flow_data_offset == -1) {
			return errh->error("No SFCBAssigner() element sets the flow context for %s !",name().c_str());
		}
		return 0;
	}

	virtual const size_t flow_data_size()  const { return sizeof(T); }

	inline void fcb_acquire() {
	    fcb_stack->acquire();
	}

	inline void fcb_release() {
	        fcb_stack->release();
	    }

	inline T* fcb_data() {
	    T* flowdata = static_cast<T*>((void*)&fcb_stack->data[_flow_data_offset]);
	    return flowdata;
	}

	void push_batch(int port,PacketBatch* head) final {
			push_batch(port, fcb_data(), head);
	};

	virtual PacketBatch* pull_batch(int port) final {
		click_chatter("ERROR : Flow Elements do not support pull");
		return 0;
	}

	virtual void push_batch(int port, T* flowdata, PacketBatch* head) = 0;





};

template<typename T, int index> class FlowSharedBufferElement : public FlowBufferElement<T> {

public :

	FlowSharedBufferElement() : FlowBufferElement<T>() {

	}

	const size_t flow_data_size()  const final { return sizeof(T); }
	const int flow_data_index()  const final { return index; }
};



#define DefineFlowSharedBuffer(name,type,index) class FlowSharedBuffer ## name ## Element : public FlowSharedBufferElement<type,index>{ };

DefineFlowSharedBuffer(Paint,int,0);
#define NR_SHARED_FLOW 1

class FlowElementVisitor : public RouterVisitor {
public:
    Element* origin;
	FlowElementVisitor(Element* e) : origin(e) {

	}

	Vector<FlowElement*> dispatchers;

	bool visit(Element *e, bool isoutput, int port,
			       Element *from_e, int from_port, int distance) {
		FlowElement* dispatcher = dynamic_cast<FlowElement*>(e);
		if (dispatcher != NULL) {
		    if (dispatcher == origin)
		        return false;
			dispatchers.push_back(dispatcher);
			return false;
		} else {

		}
        /*if (v.dispatchers[i] == (FlowElement*)e) {
            click_chatter("Classification loops are unsupported, place another FlowClassifier before reinjection of the packets.");
            e->router()->please_stop_driver();
            return 0;
        }*/
		return true;
	}

	static FlowNode* get_downward_table(Element* e,int output);
};

#endif
CLICK_ENDDECLS


#endif
