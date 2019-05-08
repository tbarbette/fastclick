// -*- c-basic-offset: 4 -*-
#ifndef CLICK_FLOWELEMENT_HH
#define CLICK_FLOWELEMENT_HH
#include <click/glue.hh>
#include <click/vector.hh>
#include <click/string.hh>
#include <click/batchelement.hh>
#include <click/routervisitor.hh>
#include "flow.hh"


CLICK_DECLS

#ifdef HAVE_FLOW

class FlowClassifier;


enum FlowType {
    FLOW_NONE = 0,
    FLOW_ETHER,
    FLOW_ARP,
    FLOW_IP,
    FLOW_TCP,
    FLOW_UDP,
    FLOW_HTTP
};

class FlowElement : public BatchElement {
public:
	FlowElement();
	~FlowElement();
	virtual FlowNode* get_table(int iport, Vector<FlowElement*> contextStack);

    FlowClassifier* _classifier;
    FlowClassifier* one_upstream_classifier() {
        return _classifier;
    }

    virtual FlowNode* resolveContext(FlowType, Vector<FlowElement*> stack);
    virtual FlowType getContext();

    virtual bool stopClassifier() { return false; };
};


/**
 * Element that needs FCB space
 */
class VirtualFlowSpaceElement : public FlowElement {
public:
	VirtualFlowSpaceElement() :_flow_data_offset(-1) {
	}
	virtual const size_t flow_data_size() const = 0;
	virtual const int flow_data_index() const {
		return -1;
	}

	inline void set_flow_data_offset(int offset) {_flow_data_offset = offset; }
	inline int flow_data_offset() {return _flow_data_offset; }

	int configure_phase() const		{ return CONFIGURE_PHASE_DEFAULT + 5; }

	void *cast(const char *name) override;
#if HAVE_FLOW_DYNAMIC
    inline void fcb_acquire(int count = 1) {
        fcb_stack->acquire(count);
    }
    inline void fcb_update(int count) {
        if (count > 0)
            fcb_stack->acquire(count);
        else if (count < 0)
            fcb_stack->release(-count);
    }

    inline void fcb_release(int count = 1) {
        fcb_stack->release(count);
    }
#else
    inline void fcb_acquire(int count = 1) {
        (void)count;
    }
    inline void fcb_update(int) {}
    inline void fcb_release(int count = 1) {
        (void)count;
    }
#endif

#if HAVE_FLOW_RELEASE_SLOPPY_TIMEOUT
    inline void fcb_acquire_timeout(int nmsec) {
        //Do not set a smaller timeout
        if ((fcb_stack->flags & FLOW_TIMEOUT) && (nmsec <= (fcb_stack->flags >> FLOW_TIMEOUT_SHIFT))) {
#if DEBUG_CLASSIFIER_TIMEOUT > 1
        click_chatter("Acquiring timeout of %p, not changing it, flag %d",this,fcb_stack->flags);
#endif
                return;
        }
#if DEBUG_CLASSIFIER_TIMEOUT > 1
        click_chatter("Acquiring timeout of %p to %d, flag %d",this,nmsec,fcb_stack->flags);
#endif
        fcb_stack->flags = (nmsec << FLOW_TIMEOUT_SHIFT) | FLOW_TIMEOUT | ((fcb_stack->flags & FLOW_TIMEOUT_INLIST) ? FLOW_TIMEOUT_INLIST : 0);
    }

    inline void fcb_release_timeout() {
#if DEBUG_CLASSIFIER_TIMEOUT > 1
        click_chatter("Releasing timeout of %p",this);
#endif
        //If the timeout is in list, we must not put it back in the pool
        if (fcb_stack->flags & FLOW_TIMEOUT_INLIST)
            assert(fcb_stack->flags & FLOW_TIMEOUT);
        if ((fcb_stack->flags & FLOW_TIMEOUT) && (fcb_stack->flags & FLOW_TIMEOUT_INLIST))
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
        fcb_release();
    }
#endif

#if HAVE_DYNAMIC_FLOW_RELEASE_FNT
    inline void fcb_set_release_fnt(struct FlowReleaseChain* fcb_chain, SubFlowRealeaseFnt fnt) {
        fcb_chain->previous_fnt = fcb_stack->release_fnt;
        fcb_chain->previous_thunk = fcb_stack->thunk;
        fcb_stack->release_fnt = fnt;
        fcb_stack->thunk = this;
#if DEBUG_CLASSIFIER_RELEASE
        click_chatter("Release fnt set to %p, was %p",fcb_stack->release_fnt,fcb_chain->previous_fnt);
#endif
    }
    inline void fcb_remove_release_fnt(struct FlowReleaseChain* fcb_chain, SubFlowRealeaseFnt fnt) {
        debug_flow("Release fnt remove %p",fnt);
        if (likely(fcb_stack->release_fnt == fnt)) { //Normally it will call the chain in the same order
            fcb_stack->release_fnt = fcb_chain->previous_fnt;
            fcb_stack->thunk = fcb_chain->previous_thunk;
            debug_flow("Release removed is now to %p",fcb_stack->release_fnt);
        } else {
            SubFlowRealeaseFnt chain_fnt = fcb_stack->release_fnt;
            VirtualFlowSpaceElement* fe = static_cast<VirtualFlowSpaceElement*>(fcb_stack->thunk);
            FlowReleaseChain* frc;
            do {
                if (fe == 0) {
                    click_chatter("BAD ERROR : Trying to remove a timeout flow function that is not set...");
                    return;
                }

                frc = reinterpret_cast<FlowReleaseChain*>(&fcb_stack->data[fe->_flow_data_offset]);
                chain_fnt = frc->previous_fnt;
                if (chain_fnt == 0) {
                    click_chatter("ERROR : Trying to remove a timeout flow function that is not set...");
                    return;
                }
                fe = static_cast<VirtualFlowSpaceElement*>(frc->previous_thunk);
            } while (chain_fnt != fnt);
            frc->previous_fnt = fcb_chain->previous_fnt;
            frc->previous_thunk = fcb_chain->previous_thunk;
        }
    }
#else
    inline void fcb_set_release_fnt(struct FlowReleaseChain* fcb, SubFlowRealeaseFnt fnt) {
        click_chatter("ERROR: YOU MUST HAVE DYNAMIC FLOW RELEASE FNT fct setted !");
        assert(false);
    }
#endif


    virtual PacketBatch* pull_batch(int port, unsigned max) override final {
        click_chatter("ERROR : Flow Elements do not support pull");
        return 0;
    }

protected:
	int _flow_data_offset;
	friend class FlowBufferVisitor;
	friend class FlowClassifier;


};

template<typename T> class FlowSpaceElement : public VirtualFlowSpaceElement {

public :

	FlowSpaceElement() CLICK_COLD;
	virtual int initialize(ErrorHandler *errh) CLICK_COLD;
    void fcb_set_init_data(FlowControlBlock* fcb, const T data) CLICK_COLD;

	virtual const size_t flow_data_size()  const { return sizeof(T); }


	/**
	 * Return the T type for a given FCB
	 */
	inline T* fcb_data_for(FlowControlBlock* fcb) {
	    T* flowdata = static_cast<T*>((void*)&fcb->data[_flow_data_offset]);
	    return flowdata;
	}

	/**
	 * Return the T type in the current FCB on the stack
	 */
	inline T* fcb_data() {
        return fcb_data_for(fcb_stack);
    }

	void push_batch(int port,PacketBatch* head) final {
			push_batch(port, fcb_data(), head);
	};
	virtual void push_batch(int port, T* flowdata, PacketBatch* head) = 0;

};

/**
 * FlowStateElement is like FlowSpaceElement but handle a timeout and a release functions
 *
 * The child must implement :
 * static const int timeout; //Timeout in msec for a flow
 * bool new_flow(T*, Packet*);
 * void push_batch(int port, T*, Packet*);
 * void release_flow(T*);
 *
 * close_flow() can be called to release the flow now, remove timer etc It will not call your release_flow(); automatically, do it before. A packet coming for the same flow after close_flow() is called will be considered from a new flow (seen flag is reset).
 */
template<class Derived, typename T> class FlowStateElement : public VirtualFlowSpaceElement {
    struct AT : public FlowReleaseChain {
        T v;
        bool seen;
    };
public :

    typedef FlowStateElement<Derived, T> derived;

    FlowStateElement() CLICK_COLD;
    virtual int initialize(ErrorHandler *errh) CLICK_COLD;
    virtual const size_t flow_data_size()  const { return sizeof(AT); }

    /**
     * CRTP virtual
     */
    inline bool new_flow(T*, Packet*) {
        return true;
    }



    /**
     * Return the T type for a given FCB
     */
    inline T* fcb_data_for(FlowControlBlock* fcb) {
        AT* flowdata = static_cast<AT*>((void*)&fcb->data[_flow_data_offset]);
        return &flowdata->v;
    }

    /**
     * Return the T type in the current FCB on the stack
     */
    inline T* fcb_data() {
        return fcb_data_for(fcb_stack);
    }

    static void release_fnt(FlowControlBlock* fcb, void* thunk ) {
        Derived* derived = static_cast<Derived*>(thunk);
        AT* my_fcb = reinterpret_cast<AT*>(&fcb->data[derived->_flow_data_offset]);
        derived->release_flow(&my_fcb->v);
        if (my_fcb->previous_fnt)
            my_fcb->previous_fnt(fcb, my_fcb->previous_thunk);
    }

    void push_batch(int port,PacketBatch* head) final {
         auto my_fcb = my_fcb_data();
         if (!my_fcb->seen) {
             if (static_cast<Derived*>(this)->new_flow(&my_fcb->v, head->first())) {
                 my_fcb->seen = true;
                 if (Derived::timeout > 0)
                     this->fcb_acquire_timeout(Derived::timeout);
                 this->fcb_set_release_fnt(my_fcb, &release_fnt);
             } else { //TODO set early drop?
                 head->fast_kill();
                 return;
             }
         }
         static_cast<Derived*>(this)->push_batch(port, &my_fcb->v, head);
    };

    void close_flow() {
        if (Derived::timeout > 0) {
            this->fcb_release_timeout();
        }
        this->fcb_remove_release_fnt(my_fcb_data(), &release_fnt);
        my_fcb_data()->seen = false;
    }

private:
    inline AT* my_fcb_data() {
        return static_cast<AT*>((void*)&fcb_stack->data[_flow_data_offset]);
    }

};



template<typename T, int index> class FlowSharedBufferElement : public FlowSpaceElement<T> {

public :

	FlowSharedBufferElement() : FlowSpaceElement<T>() {

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

	static FlowNode* get_downward_table(Element* e, int output, Vector<FlowElement*> context, bool resolve_context = false);
};


/**
 * FlowSpaceElement
 */

template<typename T>
FlowSpaceElement<T>::FlowSpaceElement() : VirtualFlowSpaceElement() {
}

template<typename T>
int FlowSpaceElement<T>::initialize(ErrorHandler *errh) {
    if (_flow_data_offset == -1) {
        return errh->error("No FlowClassifier() element sets the flow context for %s !",name().c_str());
    }
    return 0;
}

template<typename T>
void FlowSpaceElement<T>::fcb_set_init_data(FlowControlBlock* fcb, const T data) {
    for (int i = 0; i < sizeof(T); i++) {
        if (fcb->data[FCBPool::init_data_size() + _flow_data_offset + i] && fcb->data[_flow_data_offset + i] != ((uint8_t*)&data)[i]) {
            click_chatter("In %p{element} for offset %d :",this, _flow_data_offset+i);
            click_chatter("Index [%d] : Cannot set data to %d, as it is already %d",i,*((T*)(&fcb->data[_flow_data_offset])),data);
            click_chatter("Is marked as set : %d", fcb->data[FCBPool::init_data_size() + _flow_data_offset + i]);
            fcb->reverse_print();

            click_chatter("It generally means your graph is messed up");
            assert(false);
        }
        fcb->data[FCBPool::init_data_size() + _flow_data_offset + i] = 0xff;
    }
    *((T*)(&fcb->data[_flow_data_offset])) = data;
}

/**
 * FlowSpaceElement
 */

template<class Derived, typename T>
FlowStateElement<Derived, T>::FlowStateElement() : VirtualFlowSpaceElement() {
}


template<class Derived, typename T>
int FlowStateElement<Derived, T>::initialize(ErrorHandler *errh) {
    if (_flow_data_offset == -1) {
        return errh->error("No FlowClassifier() element sets the flow context for %s !",name().c_str());
    }
    return 0;
}

/**
 * Macro to define context
 *
 * In practice it will overwrite get_table
 */

#define DEFAULT_4TUPLE "12/0/ffffffff:HASH-3 16/0/ffffffff:HASH-3 20/0/ffffffff:HASH-3"

//Those should not be used anymore, as the FLOW_CONTEXT is a much alternative that assuming the top session is IP...
#define TCP_SESSION "9/06! 12/0/ffffffff:HASH-3 16/0/ffffffff:HASH-3 20/0/ffffffff:HASH-3"
#define UDP_SESSION "9/11! 12/0/ffffffff:HASH-3 16/0/ffffffff:HASH-3 20/0/ffffffff:HASH-3"

//Use only one of the 3 following macros

/**
 * Define a context (such as FLOW_IP) but no rule/session
 */
#define FLOW_ELEMENT_DEFINE_CONTEXT(ft) \
FlowNode* get_table(int iport, Vector<FlowElement*> context) override CLICK_COLD {\
    context.push_back(this);\
    return FlowElement::get_table(iport, context);\
}\
virtual FlowType getContext() override {\
    return ft;\
}\

/**
 * Define a context (such as FLOW_TCP) and a rule/session definition
 */
#define FLOW_ELEMENT_DEFINE_SESSION_CONTEXT(rule,ft) \
FlowNode* get_table(int iport, Vector<FlowElement*> contextStack) override CLICK_COLD {\
    if (ft)\
        contextStack.push_back(this);\
    FlowNode* down = FlowElement::get_table(iport,contextStack); \
    FlowNode* my = FlowClassificationTable::parse(rule).root;\
    return my->combine(down, true, true);\
}\
virtual FlowType getContext() override {\
    return ft;\
}\

/**
 * Define only a rule/session definition but no context
 */
#define FLOW_ELEMENT_DEFINE_SESSION(rule) \
        FLOW_ELEMENT_DEFINE_SESSION_CONTEXT(rule,FLOW_NONE)

/**
 * Defin two rules/sessions but no context
 */
#define FLOW_ELEMENT_DEFINE_SESSION_DUAL(ruleA,ruleB) \
FlowNode* get_table(int iport, Vector<FlowElement*> context) override CLICK_COLD {\
    return FlowClassificationTable::parse(ruleA).root->combine(FlowClassificationTable::parse(ruleB).root,false,false)->combine(FlowElement::get_table(iport,context), true,true);\
}

/**
 * Define the context no matter the input port, and a rule but only for one specific port
 */
#define FLOW_ELEMENT_DEFINE_PORT_SESSION_CONTEXT(port_num,rule,ft) \
FlowNode* get_table(int iport, Vector<FlowElement*> contextStack) override {\
    if (iport == port_num) {\
        return FlowClassificationTable::parse(rule).root->combine(FlowElement::get_table(iport,contextStack), true, true);\
    }\
    if (ft)\
        contextStack.push_back(this);\
    return FlowElement::get_table(iport,contextStack);\
}\
virtual FlowType getContext() override {\
    return ft;\
}\

#else
#define FLOW_ELEMENT_DEFINE_SESSION(rule,context)
#define FLOW_ELEMENT_DEFINE_PORT_CONTEXT(port,rule,context)
#define FLOW_ELEMENT_DEFINE_SESSION_DUAL(ruleA,ruleB,context)
#define FLOW_ELEMENT_DEFINE_SESSION_CONTEXT(rule,context)
#define FLOW_ELEMENT_DEFINE_PORT_SESSION_CONTEXT(port,rule,context)
typedef BatchElement FlowElement;
#endif
CLICK_ENDDECLS


#endif
