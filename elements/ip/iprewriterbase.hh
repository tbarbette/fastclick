// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_IPREWRITERBASE_HH
#define CLICK_IPREWRITERBASE_HH
#include <click/timer.hh>
#include "elements/ip/iprwmapping.hh"
#include <click/batchelement.hh>
#include <click/bitvector.hh>
#include <click/multithread.hh>
#include <click/error.hh>
#include <click/standard/scheduleinfo.hh>

CLICK_DECLS
class IPMapper;
class IPRewriterPattern;

class IPRewriterInput { public:
    enum {
	i_drop, i_nochange, i_keep, i_pattern, i_mapper
    };
    IPRewriterBase *owner;
    int owner_input;
    int kind;
    int foutput;
    IPRewriterBase *reply_element;
    int routput;
    uint32_t count;
    uint32_t failures;
    union {
	IPRewriterPattern *pattern;
	IPMapper *mapper;
    } u;

    IPRewriterInput()
	: kind(i_drop), foutput(-1), routput(-1), count(0), failures(0) {
	u.pattern = 0;
    }

    enum {
	mapid_default = 0, mapid_iprewriter_udp = 1
    };

    inline int rewrite_flowid(const IPFlowID &flowid,
			      IPFlowID &rewritten_flowid,
			      Packet *p, int mapid = mapid_default);
};

class IPRewriterHeap { public:

    IPRewriterHeap()
	: _capacity(0x7FFFFFFF), _use_count(1) {
    }
    ~IPRewriterHeap() {
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

    friend class IPRewriterBase;
    friend class IPRewriterFlow;

};

#define THREAD_MIGRATION_TIMEOUT 10000

/**
 * Base for Rewriter elements
 *
 * Flows are kept in a Map, implemented by y a hashtable. That is for efficient flow lookup.
 * For expiration, flows are kept in a heap.
 */
class IPRewriterBase : public BatchElement { public:

    typedef HashContainer<IPRewriterEntry> Map;
    enum {
	rw_drop = -1, rw_addmap = -2
    };

    IPRewriterBase() CLICK_COLD;
    ~IPRewriterBase() CLICK_COLD;

    enum ConfigurePhase {
	CONFIGURE_PHASE_PATTERNS = CONFIGURE_PHASE_INFO,
	CONFIGURE_PHASE_REWRITER = CONFIGURE_PHASE_DEFAULT,
	CONFIGURE_PHASE_MAPPER = CONFIGURE_PHASE_REWRITER - 1,
	CONFIGURE_PHASE_USER = CONFIGURE_PHASE_REWRITER + 1
    };

    const char *port_count() const override	{ return "1-/1-"; }
    const char *processing() const override	{ return PUSH; }

    int thread_configure(ThreadReconfigurationStage stage, ErrorHandler* errh, Bitvector threads) override;

    int configure_phase() const		{ return CONFIGURE_PHASE_REWRITER; }
    int configure(Vector<String> &conf, ErrorHandler *errh) CLICK_COLD;
    int initialize(ErrorHandler *errh) CLICK_COLD;
    void add_rewriter_handlers(bool writable_patterns);
    void cleanup(CleanupStage) CLICK_COLD;

    const IPRewriterHeap *flow_heap() const {
	return _heap[click_current_cpu_id()];
    }
    IPRewriterBase *reply_element(int input) const {
	return _input_specs[input].reply_element;
    }
    virtual HashContainer<IPRewriterEntry> *get_map(int mapid) {
	return likely(mapid == IPRewriterInput::mapid_default) ?
               &_state->map : 0;
    }

    enum {
	get_entry_check = -1, get_entry_reply = -2
    };

    inline IPRewriterEntry *search_entry(const IPFlowID &flowid);

    //Search a flow, adding it in the map if necessary
    virtual IPRewriterEntry *get_entry(int ip_p, const IPFlowID &flowid,
				       int input);

    inline void add_flow_timeout(IPRewriterFlow *flow);
    virtual IPRewriterEntry *add_flow(int ip_p, const IPFlowID &flowid,
				      const IPFlowID &rewritten_flowid,
				      int input) = 0;
    virtual void destroy_flow(IPRewriterFlow *flow) = 0;
    virtual click_jiffies_t best_effort_expiry(const IPRewriterFlow *flow) {
	return flow->expiry() +
               _timeouts[click_current_cpu_id()][0] -
               _timeouts[click_current_cpu_id()][1];
    }

    int llrpc(unsigned command, void *data);

  protected:

    unsigned _mem_units_no;

    struct IPRewriterMapState {
	IPRewriterMapState() : rebalance(0), map_lock(), map() {
        }
	    click_jiffies_t rebalance;
	    RWLock map_lock;
	    Map map;
    };
    struct IPRewriterState : public IPRewriterMapState {
	IPRewriterState() : IPRewriterMapState(), gc_timer() {
	    }
	    Timer gc_timer;
    };

    template<class T = IPRewriterState> static inline IPRewriterEntry *search_migrate_entry(const IPFlowID &flowid, per_thread<T> &vstate);

    template <class T = IPRewriterState> inline void set_migration(const bool &up, const Bitvector& threads, per_thread<T> &vstate);

    Vector<IPRewriterInput> _input_specs;
    IPRewriterHeap **_heap;
    uint32_t **_timeouts;

    uint32_t _gc_interval_sec;
    per_thread<IPRewriterState> _state;

    bool _set_aggregate;
    bool _use_cache;
    bool _handle_migration;

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

    int parse_input_spec(const String &str, IPRewriterInput &is,
			 int input_number, ErrorHandler *errh);

    enum {			// < 0 because individual patterns are >= 0
	h_nmappings = -1, h_mapping_failures = -2, h_patterns = -3,
	h_size = -4, h_capacity = -5, h_clear = -6
    };
    static String read_handler(Element *e, void *user_data) CLICK_COLD;
    static int write_handler(const String &str, Element *e, void *user_data, ErrorHandler *errh) CLICK_COLD;
    static int pattern_write_handler(const String &str, Element *e, void *user_data, ErrorHandler *errh) CLICK_COLD;

    friend int IPRewriterInput::rewrite_flowid(const IPFlowID &flowid,
			IPFlowID &rewritten_flowid, Packet *p, int mapid);

  private:

    void shift_heap_best_effort(click_jiffies_t now_j);
    bool shrink_heap_for_new_flow(IPRewriterFlow *flow, click_jiffies_t now_j);
    void shrink_heap(bool clear_all, int thid);

    friend class IPRewriterFlow;

};


class IPMapper { public:

    IPMapper()				{ }
    virtual ~IPMapper()			{ }

    virtual void notify_rewriter(IPRewriterBase *user, IPRewriterInput *input,
				 ErrorHandler *errh);
    virtual int rewrite_flowid(IPRewriterInput *input,
			       const IPFlowID &flowid,
			       IPFlowID &rewritten_flowid,
			       Packet *p, int mapid);

};


inline int
IPRewriterInput::rewrite_flowid(const IPFlowID &flowid,
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
	    reply_map = &reply_element->_state->map;
	else
	    reply_map = reply_element->get_map(mapid);
	i = u.pattern->rewrite_flowid(flowid, rewritten_flowid, *reply_map);
	goto check_for_failure;
    }
    case i_mapper:
	i = u.mapper->rewrite_flowid(this, flowid, rewritten_flowid, p, mapid);
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
IPRewriterBase::unmap_flow(IPRewriterFlow *flow, Map &map,
			   Map *reply_map_ptr)
{
    if (!reply_map_ptr)
	reply_map_ptr = &flow->owner()->reply_element->_state->map;

    Map::iterator it = map.find(flow->entry(0).hashkey());
    if (it.get() == &flow->entry(0))
	map.erase(it);

    it = reply_map_ptr->find(flow->entry(1).hashkey());
    if (it.get() == &flow->entry(1))
	reply_map_ptr->erase(it);
}

inline IPRewriterEntry *
IPRewriterBase::search_entry(const IPFlowID &flowid)
{
    return _state->map.get(flowid);
}

const bool precopy = true;

template <class T>
struct MigrationInfo {
	bool up;
	per_thread<T> *vstate;
	int from_cpu;
	IPRewriterBase* e;
};

template <class T>
bool doMigrate(Task *, void * obj) {
	click_jiffies_t jiffies = click_jiffies();
	struct MigrationInfo<T> *info = (struct MigrationInfo<T>*)obj;
	T &tstate = info->vstate->get_value_for_thread(info->from_cpu);
	T &jstate = info->vstate->get();
	click_chatter("Starting migration '%s' from core %d to core %d", (info->up?"up":"down"), info->from_cpu, click_current_cpu_id());
	if (info->up) {
		tstate.map_lock.read_begin();

		auto begin = tstate.map.begin();
		auto end = tstate.map.end();
		//jstate is not running, we do not need to acquire a lock
		for (;begin != end; begin++) {
			IPRewriterFlow *mf = static_cast<IPRewriterFlow *>(begin->flow());
			if (!begin->direction() && !mf->expired(jiffies)) {
				/*{
					auto begine = jstate.map.begin();
							auto ende = jstate.map.end();
							//jstate is not running, we do not need to acquire a lock
							for (;begine != ende; begine++) {
								click_chatter("Existing %s", begine->flowid().unparse().c_str());
							}
				}
				click_chatter("Add %s -> %s", begin->flowid().unparse().c_str(), begin->rewritten_flowid().unparse().c_str() );
				*/
				info->e->add_flow(mf->ip_p(), begin->flowid(), begin->rewritten_flowid(), mf->input());
			}
		}
		tstate.map_lock.read_end();
	} else {
		tstate.map_lock.read_begin(); //Deactivating core may be still running
		//jstate.map_lock.write_begin();
		auto begin = jstate.map.begin();
		auto end = jstate.map.end();
		for (;begin != end; begin++) {
			IPRewriterFlow *mf = static_cast<IPRewriterFlow *>(begin->flow());
			if (!begin->direction() && !mf->expired(jiffies)) {
				info->e->add_flow(mf->ip_p(), begin->flowid(), begin->rewritten_flowid(), mf->input());
			}
			//jstate.map_lock.write_relax();
		}
		//jstate.map_lock.write_end();
		tstate.map_lock.read_end();
	}
	return true;
};

template <class T> inline void
IPRewriterBase::set_migration(const bool &up, const Bitvector& threads, per_thread<T> &vstate) {

	click_jiffies_t jiffies = click_jiffies();
    if (up) {
		for (int i = 0; i < threads.size(); i++) {
			T &tstate = vstate.get_value_for_thread(i);
            if (precopy) {
		//TODO have a task that delays the effective migration
		if (threads[i]) //For each existing core
			continue;
		if (tstate.map.empty())
		    continue;
		//Copy state from i to j
		for (int j = 0; j < threads.size(); j++) {
			if (!threads[j]) //Copy to now activating core
				continue;
			click_chatter("Asking core %d to copying state of core %d to core %d",j, i, j);
			MigrationInfo<T> *info = new MigrationInfo<T>();
			info->from_cpu = i;
			info->up = up;
			info->vstate = &vstate;
			info->e = this;
			Task* t = new Task(doMigrate<T>, info, j);
			ScheduleInfo::initialize_task(this, t, ErrorHandler::default_handler());
		}
            } else {
			    if (!threads[i])
                    continue;
                click_chatter("NAT : thread %d now activated. It will fetch unknown flows from neighbour for %dms", i, THREAD_MIGRATION_TIMEOUT);
                tstate.rebalance = jiffies;
            }
		}
    } else {
		for (int i = 0; i < threads.size(); i++) {
			T &tstate = vstate.get_value_for_thread(i);
            if (precopy) {
		//In pre-copy, we copy all state of the deactivated core to the other cores
		//TODO have a task that delays the effective migration on the deactivated CPU, as they have nothing to do
		if (!threads[i]) //For each deactivating core
		    continue;
		if (tstate.map.empty())
		    continue;
		//Copy state from i to j
		for (int j = 0; j < threads.size(); j++) {
			if (threads[j]) //copy to all non-deactivating core
				continue;
			click_chatter("Asking core %d to copying state of core %d to core %d", j, i, j);
			MigrationInfo<T> *info = new MigrationInfo<T>();
			info->from_cpu = i;
			info->up = up;
			info->vstate = &vstate;
			info->e = this;
			Task* t = new Task(doMigrate<T>, info, j);
			ScheduleInfo::initialize_task(this, t, ErrorHandler::default_handler());
		}
            } else {
		if (threads[i]) {
				click_chatter("NAT : thread %d now deactivated", i);
		} else {
			click_chatter("NAT : thread %d will fetch unknown flows from neighbour for %dms", i, THREAD_MIGRATION_TIMEOUT);
		}
				tstate.rebalance = jiffies;
            }
		}
    }
}


template<class T> inline IPRewriterEntry *
IPRewriterBase::search_migrate_entry(const IPFlowID &flowid, per_thread<T> &vstate)
{
    //If the flow does not exist, it may be in other thread's stack if there was a migration
    if (vstate->rebalance > 0 && click_jiffies() - vstate->rebalance < THREAD_MIGRATION_TIMEOUT * CLICK_HZ ) {
        //Search in other thread's stacks for the flow
        for (int i = 0; i < vstate.weight(); i++) {
            if (vstate.get_mapping(i) == click_current_cpu_id())
                continue;
            T* tstate = &vstate.get_value(i);
            tstate->map_lock.read_begin();
            IPRewriterEntry *m = tstate->map.get(flowid);
            tstate->map_lock.read_end();
            if (m) {
                //click_chatter("Recovered flow from stack of thread %d", i);
                return m;
            }
        }
    }
    return 0;
}

CLICK_ENDDECLS
#endif
