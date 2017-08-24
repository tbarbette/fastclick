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
	FlowElement();
	~FlowElement();
	virtual FlowNode* get_table(int iport);

    FlowClassifier* _classifier;
    FlowClassifier* one_upstream_classifier() {
        return _classifier;
    }

};


/**
 * Element that needs FCB space
 */
class VirtualFlowBufferElement : public FlowElement {
public:
	VirtualFlowBufferElement() :_flow_data_offset(-1) {
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
	friend class FlowBufferVisitor;


};

template<typename T> class FlowBufferElement : public VirtualFlowBufferElement {

public :

	FlowBufferElement() : VirtualFlowBufferElement() {

	}

	virtual int initialize(ErrorHandler *errh) {
		if (_flow_data_offset == -1) {
			return errh->error("No FlowClassifier() element sets the flow context for %s !",name().c_str());
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

#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
	inline void fcb_acquire_timeout(int nmsec) {
	    //Do not set a smaller timeout
	    if ((fcb_stack->flags & FLOW_TIMEOUT) &&
	            nmsec < (fcb_stack->flags >> FLOW_TIMEOUT_SHIFT)) {
#if DEBUG_CLASSIFIER_TIMEOUT > 1
        click_chatter("Acquiring timeout of %p, not changing it",this);
#endif
	        return;
	    }
#if DEBUG_CLASSIFIER_TIMEOUT > 1
        click_chatter("Acquiring timeout of %p to %d",this,nmsec);
#endif
        fcb_stack->flags = (nmsec << FLOW_TIMEOUT_SHIFT) | FLOW_TIMEOUT;
	}

    inline void fcb_release_timeout() {
#if DEBUG_CLASSIFIER_TIMEOUT > 1
        click_chatter("Releasing timeout of %p",this);
#endif
        //If the timeout is in list, we must not put it back in the pool
        if ((fcb_stack->flags & FLOW_TIMEOUT) and (fcb_stack->flags & FLOW_TIMEOUT_INLIST))
            fcb_stack->flags = 0 | FLOW_TIMEOUT | FLOW_TIMEOUT_INLIST;
        else
            fcb_stack->flags = 0;
    }
#else
    inline void fcb_acquire_timeout(int nmsec) {
        //TODO : use a local timer
        fcb_acquire();
    }

    inline void fcb_release_timeout() {

    }
#endif


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

	struct inputref {
	    FlowElement* elem;
	    int iport;
	};
	Vector<struct inputref> dispatchers;

	bool visit(Element *e, bool isoutput, int port,
			       Element *from_e, int from_port, int distance) {
		FlowElement* dispatcher = dynamic_cast<FlowElement*>(e);
		if (dispatcher != NULL) {
		    if (dispatcher == origin)
		        return false;
		    struct inputref ref = {.elem = dispatcher, .iport = port};
			dispatchers.push_back(ref);
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

	static FlowNode* get_downward_table(Element* e, int output);
};

/**
 * Macro to define context
 *
 * In practice it will overwrite get_table
 */
#define FLOW_ELEMENT_DEFINE_CONTEXT(rule) \
FlowNode* get_table(int iport) override CLICK_COLD {\
    return FlowClassificationTable::parse(rule).root->combine(FlowElement::get_table(iport), true);\
}

#define FLOW_ELEMENT_DEFINE_CONTEXT_DUAL(ruleA,ruleB) \
FlowNode* get_table(int iport) override CLICK_COLD {\
    return FlowClassificationTable::parse(ruleA).root->combine(FlowClassificationTable::parse(ruleB).root,false,false)->combine(FlowElement::get_table(iport), true);\
}

#define FLOW_ELEMENT_DEFINE_PORT_CONTEXT(port_num,rule) \
FlowNode* get_table(int iport) override {\
    if (iport == port_num) {\
        return FlowClassificationTable::parse(rule).root->combine(FlowElement::get_table(iport), true);\
    }\
    return FlowElement::get_table(iport);\
}

#endif
CLICK_ENDDECLS


#endif
