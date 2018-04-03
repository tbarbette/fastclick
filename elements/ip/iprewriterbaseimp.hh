// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_IPREWRITERBASEIMP_HH
#define CLICK_IPREWRITERBASEIMP_HH
#include <click/timer.hh>
#include "elements/ip/iprwpattern.hh"
#include <click/batchelement.hh>
#include <click/bitvector.hh>
#include "iprewriterbase.hh"

CLICK_DECLS
class IPMapperIMP;
class IPRewriterPattern;

class IPRewriterHeapIMP { public:

	IPRewriterHeapIMP()
	: _capacity(0x7FFFFFFF), _use_count(1) {
    }
    ~IPRewriterHeapIMP() {
	assert(size() == 0);
    }

    void use() {
	++_use_count;
    }
    void unuse() {
	assert(_use_count > 0);
	if (--_use_count == 0)
	    delete this;
    }

    Vector<IPRewriterFlow *>::size_type size() const {
	return _heaps[0].size() + _heaps[1].size();
    }
    int32_t capacity() const {
	return _capacity;
    }

  private:

    enum {
	h_best_effort = 0, h_guarantee = 1
    };
    Vector<IPRewriterFlow *> _heaps[2];
    int32_t _capacity;
    uint32_t _use_count;

    friend class IPRewriterBaseIMP;
    friend class IPRewriterFlow;

};


class IPRewriterInputIMP : public IPRewriterInputBase { public:
	IPRewriterBaseIMP *reply_element() {
		return (IPRewriterBaseIMP *)_reply_element;
	}
	void set_reply_element(IPRewriterBaseIMP * reply) {
		_reply_element = (Element*)reply;
	}

	inline int rewrite_flowid(const IPFlowID &flowid,
					IPFlowID &rewritten_flowid,
					Packet *p, int mapid = mapid_default);

	operator IPRewriterInput& () {
		return *((IPRewriterInput*)this);
	}
};


class IPRewriterBaseIMP : public IPRewriterBaseAncestor { public:

    IPRewriterBaseIMP() CLICK_COLD;
    ~IPRewriterBaseIMP() CLICK_COLD;

    int configure(Vector<String> &conf, ErrorHandler *errh) override CLICK_COLD;
    int initialize(ErrorHandler *errh) override CLICK_COLD;
    void add_rewriter_handlers(bool writable_patterns);
    void cleanup(CleanupStage) override CLICK_COLD;

    const IPRewriterHeapIMP *flow_heap() const {
    	return heap();
    }

    IPRewriterBaseIMP *reply_element(int input) const {
    	return input_specs(input).reply_element();
    }

    virtual HashContainer<IPRewriterEntry> *get_map(int mapid) {
    	return likely(mapid == IPRewriterInput::mapid_default) ?
               &map() : 0;
    }

    enum {
	get_entry_check = -1, get_entry_reply = -2
    };
    virtual IPRewriterEntry *get_entry(int ip_p, const IPFlowID &flowid,
				       int input);
    virtual IPRewriterEntry *add_flow(int ip_p, const IPFlowID &flowid,
				      const IPFlowID &rewritten_flowid,
				      int input) = 0;
    virtual void destroy_flow(IPRewriterFlow *flow) = 0;
    virtual click_jiffies_t best_effort_expiry(const IPRewriterFlow *flow) override {
    	return ((IPRewriterFlow*)flow)->expiry() +
               timeouts()[0] -
               timeouts()[1];
    }

    int llrpc(unsigned command, void *data);


    inline Map& map() {
    	return _state->map;
    }

  protected:

    inline unsigned mem_units_no() {
    	return _state.weight();
    }



    inline const uint32_t* timeouts() const {
		return _state->timeouts;
	}

    void initialize_timeout(int idx, uint32_t val) {
    	PER_THREAD_MEMBER_SET(_state, timeouts[idx], val);
    }

    inline IPRewriterHeapIMP *& heap() {
    	return _state->heap;
    }

    inline IPRewriterHeapIMP *& heap() const {
    	return _state->heap;
    }

    inline Timer & gc_timer() {
    	return _state->gc_timer;
    }

    inline IPRewriterInputIMP& input_specs(int input) const {
    	return (IPRewriterInputIMP&)_input_specs[input];
    }

    inline IPRewriterInputIMP& input_specs_unchecked(int input) const {
    	return (IPRewriterInputIMP&)_input_specs.unchecked_at(input);
    }

    inline int input_specs_size() const {
    	return _input_specs.size();
    }

    uint32_t _gc_interval_sec;


    enum {
	default_timeout = 300,	   // 5 minutes
	default_guarantee = 5,	   // 5 seconds
	default_gc_interval = 60 * 15 // 15 minutes
    };

    static uint32_t relevant_timeout(const uint32_t timeouts[2]) {
	return timeouts[1] ? timeouts[1] : timeouts[0];
    }

    IPRewriterEntry *store_flow(IPRewriterFlow *flow, int input,
				Map &map, Map *reply_map_ptr = 0);
    inline void unmap_flow(IPRewriterFlow *flow,
			   Map &map, Map *reply_map_ptr = 0);

    static void gc_timer_hook(Timer *t, void *user_data);

    int parse_input_spec(const String &str, IPRewriterInputIMP &is,
			 int input_number, ErrorHandler *errh);

    enum {			// < 0 because individual patterns are >= 0
	h_nmappings = -1, h_mapping_failures = -2, h_patterns = -3,
	h_size = -4, h_capacity = -5, h_clear = -6
    };
    static String read_handler(Element *e, void *user_data) CLICK_COLD;
    static int write_handler(const String &str, Element *e, void *user_data, ErrorHandler *errh) CLICK_COLD;
    static int pattern_write_handler(const String &str, Element *e, void *user_data, ErrorHandler *errh) CLICK_COLD;

    void dump_mappings(StringAccum& sa);

    friend int IPRewriterInput::rewrite_flowid(const IPFlowID &flowid,
    			IPFlowID &rewritten_flowid, Packet *p, int mapid);

  private:
    class ThreadState { public:
		IPRewriterHeapIMP *heap;
		uint32_t timeouts[2];
		Map map;
		Timer gc_timer;
		ThreadState();
		~ThreadState();
    };

    per_thread<ThreadState> _state;
    Vector<IPRewriterInput> _input_specs;

    void shift_heap_best_effort(click_jiffies_t now_j);
    bool shrink_heap_for_new_flow(IPRewriterFlow *flow, click_jiffies_t now_j);
    void shrink_heap(bool clear_all, int thid);

    friend class IPRewriterFlow;

};

class IPMapperIMP : public IPMapper { public:

	IPMapperIMP()				{ }
    virtual ~IPMapperIMP()			{ }

    virtual void notify_rewriter(IPRewriterBaseAncestor *user, IPRewriterInput *input,
				 ErrorHandler *errh) override;
    virtual int rewrite_flowid(IPRewriterInputAncestor *input,
			       const IPFlowID &flowid,
			       IPFlowID &rewritten_flowid,
			       Packet *p, int mapid) override;

};


inline int
IPRewriterInputIMP::rewrite_flowid(const IPFlowID &flowid,
				IPFlowID &rewritten_flowid,
				Packet *p, int mapid)
{
    int i;
    switch (kind) {
      case i_nochange:
    	return foutput;
      case i_keep:
		rewritten_flowid = flowid;
		return IPRewriterBase::rw_addmap;
      case i_pattern: {
		HashContainer<IPRewriterEntry> *reply_map;
		if (likely(mapid == mapid_default))
			reply_map = &reply_element()->map();
		else
			reply_map = reply_element()->get_map(mapid);
		i = u.pattern_imp->rewrite_flowid(flowid, rewritten_flowid, *reply_map);
		goto check_for_failure;
      }
      case i_mapper:
		i = u.mapper_imp->rewrite_flowid((IPRewriterInput*)this, flowid, rewritten_flowid, p, mapid);
		goto check_for_failure;
		check_for_failure:
		if (i == IPRewriterBase::rw_drop)
			++failures;
		return i;
      default:
    	return IPRewriterBase::rw_drop;
    }
}


inline void
IPRewriterBaseIMP::unmap_flow(IPRewriterFlow *flow, Map &map,
			   Map *reply_map_ptr)
{
    //click_chatter("kill %s", hashkey().s().c_str());
    if (!reply_map_ptr)
    	reply_map_ptr = &flow->ownerimp()->reply_element()->map();

    Map::iterator it = map.find(flow->entry(0).hashkey());
    if (it.get() == &flow->entry(0))
	map.erase(it);

    it = reply_map_ptr->find(flow->entry(1).hashkey());
    if (it.get() == &flow->entry(1))
	reply_map_ptr->erase(it);
}

CLICK_ENDDECLS
#endif
