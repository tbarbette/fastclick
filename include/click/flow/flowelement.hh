// -*- c-basic-offset: 4 -*-
#ifndef CLICK_FLOWELEMENT_HH
#define CLICK_FLOWELEMENT_HH
#include <click/glue.hh>
#include <click/vector.hh>
#include <click/string.hh>
#include <click/batchelement.hh>
#include <click/routervisitor.hh>
#include <click/pair.hh>
#include "flow.hh"


CLICK_DECLS

#ifdef HAVE_FLOW

class VirtualFlowManager;

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

    virtual FlowType getContext();

    virtual bool stopClassifier() { return false; };
};


/**
 * Element that needs FCB space
 */
class VirtualFlowSpaceElement : public FlowElement, Router::InitFuture {
public:
    VirtualFlowSpaceElement() :_flow_data_offset(-1) {
    }
    virtual const size_t flow_data_size() const = 0;
    virtual const int flow_data_index() const {
        return -1;
    }

    inline void set_flow_data_offset(int offset) {_flow_data_offset = offset; }
    inline int flow_data_offset() {return _flow_data_offset; }

    int configure_phase() const        { return CONFIGURE_PHASE_DEFAULT + 5; }

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

    int initialize(ErrorHandler *errh) override CLICK_COLD {
	//The element itself is automatically posted by build_fcb via  fcb_builded_init_future
	return 0;
    }
protected:

    int _flow_data_offset;
    friend class FlowBufferVisitor;
    friend class VirtualFlowManager;
};

/**
 * This future will only trigger once it is called N times.
 * N is increased by calling add(). The typical usage is a future
 * that will only trigger when all parents have called. To do this,
 * you call add() in the constructor of the parents.
 */
class CounterInitFuture : public Router::InitFuture { public:
    CounterInitFuture(String name, std::function<void(void)> on_reached) : _n(0), _name(name), _on_reached(on_reached) {


    }

    void add() {
        _n ++;
    }

    virtual void post(Router::InitFuture* future) {
        Router::InitFuture::post(future);
    }

    virtual int solve_initialize(ErrorHandler* errh) {
        if (--_n == 0) {
            _on_reached();
            return Router::InitFuture::solve_initialize(errh);
        }
        return 0;
    }

private:
    int _n;
    String _name;
    std::function<void(void)> _on_reached;
};

/**
 * Element that allocates some FCB Space
 */
class VirtualFlowManager : public FlowElement { public:
    VirtualFlowManager();
protected:
    int _reserve;

    typedef Pair<Element*,int> EDPair;
    Vector<EDPair>  _reachable_list;

    static Vector<VirtualFlowManager*> _entries;
    static CounterInitFuture _fcb_builded_init_future;

    void find_children(int verbose = 0);

    static void _build_fcb(int verbose,  bool ordered);
    static void build_fcb();

    bool stopClassifier() { return true; };
};

template<typename T> class FlowSpaceElement : public VirtualFlowSpaceElement {

public :

    FlowSpaceElement() CLICK_COLD;
    virtual int solve_initialize(ErrorHandler *errh) override CLICK_COLD;
    void fcb_set_init_data(FlowControlBlock* fcb, const T data) CLICK_COLD;

    virtual const size_t flow_data_size()  const override { return sizeof(T); }


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

    void push_batch(int port, PacketBatch* head) final {
            push_flow(port, fcb_data(), head);
    };

    virtual void push_flow(int port, T* flowdata, PacketBatch* head) = 0;
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
    virtual int solve_initialize(ErrorHandler *errh) CLICK_COLD;
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

    void push_batch(int port, PacketBatch* head) {
         auto my_fcb = my_fcb_data();
         if (!my_fcb->seen) {
             if (static_cast<Derived*>(this)->new_flow(&my_fcb->v, head->first())) {
                 my_fcb->seen = true;
                 if (Derived::timeout > 0)
                     this->fcb_acquire_timeout(Derived::timeout);
#if HAVE_DYNAMIC_FLOW_RELEASE_FNT
                 this->fcb_set_release_fnt(my_fcb, &release_fnt);
#endif
             } else { //TODO set early drop?
                 head->fast_kill();
                 return;
             }
         }
         static_cast<Derived*>(this)->push_flow(port, &my_fcb->v, head);
    };

    void close_flow() {
        if (Derived::timeout > 0) {
            this->fcb_release_timeout();
        }
#if HAVE_DYNAMIC_FLOW_RELEASE_FNT
        this->fcb_remove_release_fnt(my_fcb_data(), &release_fnt);
#endif
        my_fcb_data()->seen = false;
    }

private:
    inline AT* my_fcb_data() {
        return static_cast<AT*>((void*)&fcb_stack->data[_flow_data_offset]);
    }

};

/**
 * FlowSpaceElement
 */

template<typename T>
FlowSpaceElement<T>::FlowSpaceElement() : VirtualFlowSpaceElement() {
}

template<typename T>
int
FlowSpaceElement<T>::solve_initialize(ErrorHandler *errh) {
    if (_flow_data_offset == -1) {
        return errh->error("No FlowManager() element sets the flow context for %s !",name().c_str());
    }
    return 0;
}


template<class Derived, typename T>
FlowStateElement<Derived, T>::FlowStateElement() : VirtualFlowSpaceElement() {
}


template<class Derived, typename T>
int FlowStateElement<Derived, T>::solve_initialize(ErrorHandler *errh) {
    if (_flow_data_offset == -1) {
        return errh->error("No FlowManager() element sets the flow context for %s !",name().c_str());
    }
    return 0;
}

#endif

CLICK_ENDDECLS


#endif
