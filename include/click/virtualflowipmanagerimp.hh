#ifndef CLICK_VIRTUALFLOWIPMANAGERIMP_HH
#define CLICK_VIRTUALFLOWIPMANAGERIMP_HH
#include <click/batchbuilder.hh>
#include <click/config.h>
#include <click/flow/common.hh>
#include <click/flow/flowelement.hh>
#include <click/multithread.hh>
#include <click/pair.hh>
#include <click/string.hh>
#include <click/timer.hh>
#include <click/timerwheel.hh>
#include <click/vector.hh>
#include <stack>
#include <mutex>
#include <shared_mutex>
#include <click/args.hh>
#include <rte_malloc.h>
#include <cpuid.h>


/**
 * Virtual class for all FlowIPManager elements
 * The template arguments determine how it is specialized:
 *  - lock determines how the flow stack should be protected
 *	    IMP methods would set the lock to the "nolock" class.
 *  - is_imp determines if we use a single table for all threads or
 *      core-sharding
 *  - deferred_free determines if the current HT has different deletion and
 *free e.g. DPDK Cuckoo implementation has deletion (do not use that entry) and
 *free (remove the entry from the HT)
 *  - MaintainerArg,a series of constant parameters for the Maintainer, that is the method used to reycle old flows.
 *
 *  Compilation flags that you may want to set
 *
 *  - FLOWIPMANAGER_PRINT If set to 0 it will disable all prints (also errors!)
 *			Default is 1 (print messages)
 *  - MAINTAINER_PRINT_CYCLES If set to 1 will print cycles when recyclint
 *  			(to be used with NPF)
 *  			Default is 0 (do not print)
 *  - IMP_COUNTERS If set to 1 enable counters about the operations.
 *			Otherwise disabled. Counters slow down the element due
 *			to TSC register reads.
 *			Default is 0 (do not count)
 *  - IMP_COUNTERS_TOTAL If set to 0 do not measure the total cycles for the
 *			process function
 *			Default is 1 (If IMP_COUNTERS is set, measure it)
 *
 *  - HAVE_CYCLES If IMP_COUNTERS is set, this can be set to 0 to not count
 *			cycles but only insert/lookup/deletion times.
 *			Default is 1 (measure cycles)
 *  - HAVE_CYCLES_BARRIER whether we want to be strict on cycle measurement.
 *			It inserts a lfence before or after the TSC reads
 *			Default is 0 (do not be strict)
 *  - UPDATE_THRESHOLD	determines how frequent the counter averages are updated
 *			in terms of "number of lookups done from last update"
 *			Default is 65536
 *  - LAZY_INSERT_FULL	FOR LAZY METHODS: if the table is full, try to insert
 *			Default is 1, if tracking the load of the table
 *			you may want to disable it (e.g. when plotting
 *			cycles vs. load of the table)
 *
 * */

#define DBG_PRINT() (void)0;

#ifndef FLOWIPMANAGER_PRINT
#define FLOWIPMANAGER_PRINT 1
#endif

#ifndef MAINTAINER_PRINT_CYCLES
#define MAINTAINER_PRINT_CYCLES 0
#endif

#ifndef IMP_COUNTERS
#define IMP_COUNTERS 0
#endif

#ifndef IMP_COUNTERS_TOTAL
#define IMP_COUNTERS_TOTAL 1
#endif

#ifndef HAVE_CYCLES
#define HAVE_CYCLES 1
#endif

#ifndef HAVE_CYCLES_BARRIER
#define HAVE_CYCLES_BARRIER 0
#endif

#ifndef UPDATE_THRESHOLD
#define UPDATE_THRESHOLD 65536
#endif

#ifndef LAZY_INSERT_FULL
#define LAZY_INSERT_FULL 1
#endif

#ifndef BATCH_FLOWSTACK
#define BATCH_FLOWSTACK 1
#endif

#if FLOWIPMANAGER_PRINT
#define VPRINT(level, ...)                                                     \
    {                                                                          \
        if (unlikely(_verbose >= level)) click_chatter(__VA_ARGS__);           \
    }
#else
#define VPRINT(level, ...)
#endif

// The Hash function for the ipv4 5-tuple
#define hash_flow(F) ipv4_hash_crc(&F, sizeof(uint16_t), 0);

#if HAVE_CYCLES
#if HAVE_CYCLES_BARRIER
#define TSC_BARRIER()                                                          \
    { asm volatile("lfence"); }
inline click_cycles_t get_cycles_pre() {
    TSC_BARRIER();
    return click_get_cycles();
}
inline click_cycles_t get_cycles_post() {
    click_cycles_t ret = click_get_cycles();
    TSC_BARRIER();
    return ret;
}
#else
#define get_cycles_pre() click_get_cycles()
#define get_cycles_post() click_get_cycles()
#endif
#else
#define get_cycles_pre() 0
#define get_cycles_post() 0
#endif

#if IMP_COUNTERS
#define KILL_PACKET(p, core)                                                   \
    {                                                                          \
        VPRINT(2, "Cannot insert, table %i is full! (at %s:%i)", core,         \
               __FILE__, __LINE__);                                            \
        _tables[core].killed_packets++;                                        \
        p->kill();                                                             \
    }

#if IMP_COUNTERS_TOTAL
// Define some macros to measure cycles in process()
#define CYCLES_PROCESS_START                                                   \
    int real_processor = click_current_cpu_id();                               \
    click_cycles_t total_cycles = get_cycles_pre();
#define CYCLES_PROCESS_PRE_LOOKUP click_cycles_t t = total_cycles;
#define CYCLES_PROCESS_END                                                     \
    _tables[real_processor].cycles_total += (get_cycles_post() - total_cycles);

#else
#define CYCLES_PROCESS_START int real_processor = click_current_cpu_id();
#define CYCLES_PROCESS_PRE_LOOKUP click_cycles_t t = get_cycles_pre()
#define CYCLES_PROCESS_END
#endif

#define CYCLES_PROCESS_POST_LOOKUP                                             \
    _tables[real_processor].cycles_lookups += (get_cycles_post() - t);         \
    _tables[real_processor].this_lookups++;

#define CYCLES_PROCESS_PRE_INSERT click_cycles_t t = get_cycles_pre();
#define CYCLES_PROCESS_POST_INSERT                                             \
    _tables[real_processor].cycles_inserts += (get_cycles_post() - t);         \
    _tables[real_processor].this_inserts++;

#else

#define CYCLES_PROCESS_START
#define CYCLES_PROCESS_PRE_LOOKUP
#define CYCLES_PROCESS_POST_LOOKUP
#define CYCLES_PROCESS_PRE_INSERT
#define CYCLES_PROCESS_POST_INSERT
#define CYCLES_PROCESS_END

#define KILL_PACKET(p, core)                                                   \
    {                                                                          \
        VPRINT(2, "Cannot insert, table %i is full! (at %s:%i)", core,         \
               __FILE__, __LINE__);                                            \
        p->kill();                                                             \
    }
#endif

#define FCB_DATA(fcb, offset) (((uint8_t *)(fcb->data_32)) + offset)

CLICK_DECLS

struct MaintainerArg {
  bool enabled; //Is the maintainer enabled
  bool is_imp; //Is the maintainer duplicated per-thread
  bool no_fid; //TODO
};


constexpr MaintainerArg maintainerArgNone { false, false, false };
constexpr MaintainerArg maintainerArgMP { true, false, false };
constexpr MaintainerArg maintainerArgIMP { true, true, false };
constexpr MaintainerArg maintainerArgIMPNOFID { true, true, true };



class DPDKDevice;


// We don't want to have that long lines around each function...
#define VFIM_TEMPLATE                                                          \
    template <typename Lock, bool is_imp, bool have_deferred_free,             \
              MaintainerArg const& have_maintainer>
#define VFIM_CLASS                                                             \
    VirtualFlowIPManagerIMP<Lock, is_imp, have_deferred_free, have_maintainer>

template <typename Lock = nolock, bool is_imp = true,
          bool have_deferred_free = false, MaintainerArg const& have_maintainer = maintainerArgNone>
class VirtualFlowIPManagerIMP : public VirtualFlowManager,
                                public Router::InitFuture {
  public:
    VirtualFlowIPManagerIMP<Lock, is_imp, have_deferred_free, have_maintainer>()
        CLICK_COLD;
    ~VirtualFlowIPManagerIMP<Lock, is_imp, have_deferred_free,
                             have_maintainer>() CLICK_COLD;

    virtual const char *class_name() const override = 0;
    const char *port_count() const override { return "1/1"; }

    const char *processing() const override { return PUSH; }
    int configure_phase() const override {
        return CONFIGURE_PHASE_PRIVILEGED + 1;
    }
    bool stopClassifier() { return true; };

    int configure(Vector<String> &conf, ErrorHandler *errh) override CLICK_COLD;

    // To be redefined in derived classes for additional arguments
    virtual int parse(Args *args) { return 0; };

    // Our configuration parser
    int base_parse(Args *args) CLICK_COLD;

    int solve_initialize(ErrorHandler *errh) override CLICK_COLD;
    // This is the real method to be called by each thread
    virtual int _per_thread_initialize(ErrorHandler *errh, int i) CLICK_COLD;
    // This is a wrapper to the above. If is_imp is set, it will call for each
    // thread, otherwise it will be called only the first time
    virtual int per_thread_initialize(ErrorHandler *errh, int i) CLICK_COLD;

    void cleanup(CleanupStage stage) override CLICK_COLD{};

    // TODO: deallocate the structures
    void push_batch(int, PacketBatch *batch) override;

    // Read handlers
    static String read_handler(Element *e, void *thunk);
    static int write_handler(const String &input, Element *e, void *thunk,
                             ErrorHandler *errh);
    virtual void add_handlers() override;

    // The hash function
    struct hash_flow_T {
        std::size_t operator()(const IPFlow5ID &f) const {
            return hash_flow(f);
        }
    };

    // Maintainer is the method that will delete the entries in the table
    // If a table implements a lazy deletion, you want to override it with
    // an empty (or custom) logic.
    // This method will be called by a timer by default.
    virtual int maintainer(int core);

  protected:
    // The core of the element!
    virtual void process(Packet *p, BatchBuilder &b, Timestamp &recent,
                         int core);

    // The functions to be redefined
    // Either redefine the non-packet version or the packet one.
    virtual int find(IPFlow5ID &f, int core) = 0;
    virtual int insert(IPFlow5ID &f, int flowid, int core)  = 0;

    // Allocation functions
    // Use alloc to re-define your allocation for the table
    virtual int alloc(int i) = 0;
    // Redefine alloc_fcb if you need a special allocation for FlowControlBlock
    virtual int alloc_fcb(int core);
    inline static const int reserve_size() {
        // FlowID field and 5-tuple (if we are deleting)
        // if (have_deletion)
        return sizeof(uint32_t) + sizeof(IPFlow5ID);
        // else
        // return sizeof(uint32_t);
    }

    virtual uint64_t max_key_id(int core) {
        return (_table_size + 1);
    }

#if BATCH_FLOWSTACK
    virtual int local_flow_stacks_init() CLICK_COLD;
    inline uint32_t local_flows_pop(int core, int localcore);
    inline void local_flows_push(int core, int localcore, uint32_t flow);
    inline bool local_flows_empty(int core, int localcore);
    inline uint32_t imp_flows_pop(int core);
    inline void imp_flows_push(int core, uint32_t flow);
    inline bool imp_flows_empty(int core);
    int get_flowbatch(int core);
#endif
    // Flow ID handling. Called in insert/deletion
    // Flows are managed as a stack.
    // NO THREAD SAFE! The caller must ensure protection
    inline uint32_t flows_pop(int core);
    inline void flows_push(int core, uint32_t flow, const bool init = false);
    inline bool flows_empty(int core);
    virtual inline bool is_full(int core);
    // Do we want to insert if the table is full? Typically no.
    // Lazy methods should redefine accordingly to LAZY_INSERT_FULL
    virtual inline bool insert_if_full() { return false; }

    // This is lock-protected inside!
    inline uint32_t get_next(int core);

    // method to call to verify whethet we may insert ot not
    // derivated class may extend to implement custom controls
    // e.g. lazy deletion will never be full

    // How many elements are in the table?
    virtual int get_count();
    virtual int get_count(int core);

    // Average load of the table
    virtual double get_avg_load();

#if IMP_COUNTERS
    void update_averages(int core = 0);
    String get_counter(int cnt);
#endif

    // Deletion methods
    virtual inline int delete_flow(FlowControlBlock *fcb, int core = 0) = 0;
    virtual inline int free_pos(int pos, int core = 0) { return 0; };

    // What is the size of each table?
    virtual int table_size_per_thread(int size, int threads);
    virtual int total_table_size(int size, int threads);

    // Access helper methods
    inline FlowControlBlock *get_fcb(int i, int core = 0);
    virtual inline void update_lastseen(FlowControlBlock *fcb, Timestamp &ts);

    bool _have_deferred_free; // Are we in a deferred_free situation (to be read
                              // by derivates)
    int32_t _table_size;      // The actual table size
    int32_t _total_size;      // The actual table size (grand total)
    int _flow_state_size_full; // How big is each entry
    int _verbose;
    bool _cache;                   // Are we caching?
    int _tables_count;             // How many tables in total (size of tables)
    int _passing_weight;           // How many threads do we have
    uint16_t _timeout;             // Timeout for deletion
    uint32_t _timeout_ms;          // Timeout for deletion
    uint32_t _epochs_per_sec;      // Granularity for the epoch
    uint32_t _scan_per_epoch;      // How many scans per epoch
    uint16_t _recycle_interval_ms; // When to run the maintainer

#if BATCH_FLOWSTACK
    int _flows_batchsize;
    int _flows_total;
    int _flows_hithresh;
    int _flows_lothresh;
#endif


    // Per thread data-structure.
    // Single thread methods will use the table at index 0
    // But update the counters at their index (if IMP_COUNTERS is 1)
    struct gtable {
        FlowControlBlock *fcbs = 0;
        void *hash = 0;
        // The timer for mainteinance (per-thread)
        Timer *maintain_timer = 0;
        // Flow stack management
        uint32_t *flows_stack = 0;
        int flows_stack_i = -1;
        Lock *lock;
#if BATCH_FLOWSTACK
        int * local_flowstacks_i = 0;
        uint32_t ** local_flowstacks = 0;
#endif

        uint32_t current = 0;

#if IMP_COUNTERS
        uint64_t count_inserts = 0;
        uint64_t count_lookups = 0;
        uint64_t cycles_inserts = 0;
        uint64_t cycles_lookups = 0;
        uint64_t avg_cycles_inserts = 0;
        uint64_t avg_cycles_lookups = 0;
        uint64_t this_inserts = 0;
        uint64_t this_lookups = 0;
        uint64_t killed_packets = 0;
        uint64_t matched_packets = 0;
        uint64_t maintain_removed = 0;
#if IMP_COUNTERS_TOTAL
        uint64_t cycles_total = 0;
        uint64_t avg_cycles_total = 0;
#endif
#endif
    } CLICK_ALIGNED(CLICK_CACHE_LINE_SIZE);

    gtable *_tables;

  public:
    // Helper functions for the maintainance. Assume that expiry and hash fields
    // exists!
    static inline uint32_t *get_flowid(FlowControlBlock *fcb) {
        return (uint32_t *)FCB_DATA(fcb, 0);
    };
    static inline IPFlow5ID *get_fid(FlowControlBlock *fcb) {
        return (IPFlow5ID *)FCB_DATA(fcb, sizeof(uint32_t));
    };

    // Helper to visualize the stack (for debug purposes)
    void print_stack(int core);

    // Timer stuff
    static void run_maintain_timer(Timer *t, void *thunk) {
        int core = click_current_cpu_id();
        VirtualFlowIPManagerIMP *e =
            reinterpret_cast<VirtualFlowIPManagerIMP *>(thunk);

#if MAINTAINER_PRINT_CYCLES
        int start = get_cycles_pre();
        Timestamp now_precise = Timestamp::now();
#endif

	 int removed = e->maintainer(is_imp || have_maintainer.is_imp ? core : 0);
#if MAINTAINER_PRINT_CYCLES
        int stop = get_cycles_post();
        click_chatter("%s-RESULT-MAINTAIN_CYCLES %i",
                      now_precise.unparse().c_str(), stop - start);
        click_chatter("%s-RESULT-MAINTAIN_RECYCLED %i",
                      now_precise.unparse().c_str(), removed);
#endif

#if IMP_COUNTERS
        e->_tables[core].maintain_removed += removed;
#endif
        e->_tables[core].maintain_timer->schedule_after_msec(
            e->_recycle_interval_ms);
    }
};

VFIM_TEMPLATE
VFIM_CLASS::VirtualFlowIPManagerIMP() : _verbose(0) {
    _have_deferred_free = have_deferred_free;
}

VFIM_TEMPLATE
VFIM_CLASS::~VirtualFlowIPManagerIMP() {}

VFIM_TEMPLATE
int VFIM_CLASS::base_parse(Args *args) {
    double recycle_interval = 0;
    int ret =
        (*args)
            .read_or_set_p("CAPACITY", _table_size, 65536) // HT capacity
            .read_or_set("RESERVE", _reserve, 0)
            .read_or_set("VERBOSE", _verbose, 0)
            .read_or_set("CACHE", _cache, 1)
            .read_or_set("TIMEOUT", _timeout, 0) // Timeout for the entries
            .read_or_set("RECYCLE_INTERVAL", recycle_interval, 1)
#if BATCH_FLOWSTACK
	    .read_or_set("FLOW_BATCH", _flows_batchsize, 16)
#endif
            .consume();

    _recycle_interval_ms = (int)(recycle_interval * 1000);
    _epochs_per_sec = max(1, 1000 / _recycle_interval_ms);
    _timeout_ms = _timeout * 1000;

    return ret;
}

VFIM_TEMPLATE
int VFIM_CLASS::configure(Vector<String> &conf, ErrorHandler *errh) {
    Args args(conf, this, errh);

    if (base_parse(&args) || parse(&args) || args.complete())
        return errh->error("Error while parsing arguments!");

    VPRINT(2, "RESERVE %d -> %d", _reserve, reserve_size());
    _reserve += reserve_size();

    if (!have_maintainer.enabled)
        errh->warning("This element is not managing flow release ! It is only to be used for datastructure evaluation!");
    find_children(_verbose);
    router()->get_root_init_future()->postOnce(&_fcb_builded_init_future);
    _fcb_builded_init_future.post(this);
    return 0;
}

// solve_initializion portion per-thread
// This should be overriden in shared table methods to do it only for core=0
VFIM_TEMPLATE
int VFIM_CLASS::_per_thread_initialize(ErrorHandler *errh, int core) {
    click_chatter("Core %d", core);
    if (alloc(core))
        return errh->error("Error while initializing HT");

    if (alloc_fcb(core))
        return errh->error("Error while allocating fcbs table");

    if (!have_maintainer.no_fid) {
        click_chatter("[%i] Initializing flow ids stack (sz %lu + 1)",core, _table_size);
        _tables[core].flows_stack = (uint32_t *)rte_zmalloc(
            "FLOWS_STACK", sizeof(uint32_t) * (_table_size + 1), CLICK_CACHE_LINE_SIZE);
        assert(_tables[core].flows_stack != NULL);

    #if BATCH_FLOWSTACK
        local_flow_stacks_init();
    #endif
        for (uint32_t j = _table_size; j > 0; j--) {
            flows_push(core, j, true);
        }
    }

    return 0;
}

VFIM_TEMPLATE
int VFIM_CLASS::per_thread_initialize(ErrorHandler *errh, int core) {
    VPRINT(4, "%s for core %i", __FUNCTION__, core);
    if (is_imp) {
        return _per_thread_initialize(errh, core);
    } else if (_tables[0].hash == 0) {
        VPRINT(3, "Initializing (first time)");
	return _per_thread_initialize(errh, 0);
    }

    VPRINT(3, "Skip initialization (thread %i)", core);
    return 0;
}

VFIM_TEMPLATE
int VFIM_CLASS::solve_initialize(ErrorHandler *errh) {
    _flow_state_size_full = sizeof(FlowControlBlock) + _reserve;
    VPRINT(2, "Each flow entry will be %i bytes", _flow_state_size_full);

    auto passing = get_passing_threads();
    _tables_count = passing.size();
    assert(_tables_count > 0);
    _passing_weight = passing.weight();
    _table_size = table_size_per_thread(_table_size, _passing_weight);
    _total_size = total_table_size(_table_size, _passing_weight);
    _scan_per_epoch = _table_size / _epochs_per_sec;

    _tables = (gtable *)rte_zmalloc("GTABLES", sizeof(gtable) * (_tables_count),
                                    CLICK_CACHE_LINE_SIZE);

    assert(_tables != NULL);

    VPRINT(1, "Real capacity for each table will be %lu", _table_size);

    for (int core = 0; core < _tables_count; core++) {
        click_chatter("Initializing core %d/%d", core, _tables_count);
        _tables[core].lock = new Lock(); //Remember Lock may be an instance of nolock
        _tables[core].lock->acquire();
        _tables[core].lock->release();
        if (!passing[core]) {
            _tables[core].flows_stack_i = -2;
            continue;
        }

	if (_timeout > 0 && have_maintainer.enabled && (core == 0 || have_maintainer.is_imp || is_imp)) {

	  click_chatter("Initializing maintain timer %d", core);
	  _tables[core].maintain_timer = new Timer(this);
	  _tables[core].maintain_timer->initialize(this, true);
	  _tables[core].maintain_timer->assign(run_maintain_timer, this);
	  _tables[core].maintain_timer->move_thread(core);
	  _tables[core].current = 0;
	  _tables[core].maintain_timer->schedule_after_msec(_recycle_interval_ms);
	}

        if (per_thread_initialize(errh, core))
            return errh->error("Error while allocating FCBs on core %i", core);
    }

    // if(_verbose>2)
    // {
    //       rte_malloc_get_socket_stats(0, stats_after);
    // click_chatter("Memory after initialization: TOTAL %lu, ALLOCATED %lu,
    // FREE %lu", stats_after->heap_totalsz_bytes,
    // stats_after->heap_freesz_bytes,
    // stats_after->heap_allocsz_bytes);
    // click_chatter("Allocated a total of %lu B (%f GB) = %f of the total heap.
    // %f free", stats_after->heap_allocsz_bytes -
    // stats_before->heap_allocsz_bytes, (stats_after->heap_allocsz_bytes -
    // stats_before->heap_allocsz_bytes)/(1024.0*1024.0*1024.0),
    // stats_after->heap_allocsz_bytes / (float)
    // stats_after->heap_totalsz_bytes, stats_after->heap_freesz_bytes / (float)
    // stats_after->heap_totalsz_bytes);
    // }

    VPRINT(1, "Initialized %s", class_name());

    return Router::InitFuture::solve_initialize(errh);
}

VFIM_TEMPLATE
void VFIM_CLASS::push_batch(int, PacketBatch *batch) {
    BatchBuilder b;
    Timestamp recent = Timestamp::recent_steady();

    FOR_EACH_PACKET_SAFE(batch, p) {
        if (is_imp) {
            process(p, b, recent, click_current_cpu_id());
        } else
            process(p, b, recent, 0);
    }

    batch = b.finish();
    if (batch) {
        output_push_batch(0, batch);
    }
}

// The actual logic of this element
VFIM_TEMPLATE
inline void VFIM_CLASS::process(Packet *p, BatchBuilder &b, Timestamp &recent,
                                int core) {
    // Here we use CYCLES_PROCESS_* mactos to measure cycles and counters
    // These are copy-pasted here (note no parenenthesis)
    // It helps to keep the code more readable.
    CYCLES_PROCESS_START

    IPFlow5ID fid = IPFlow5ID(p);
    if (_cache && fid == b.last_id) {
        click_chatter("Cache");
        b.append(p);
        return;
    }

    FlowControlBlock *fcb;

    // LOOKUP ON THE TABLE
    CYCLES_PROCESS_PRE_LOOKUP
    int ret = find(fid, core);
    CYCLES_PROCESS_POST_LOOKUP

    if (ret == 0) {

        uint32_t flowid;
        if (!have_maintainer.no_fid) {
            flowid = get_next(core);
            if (unlikely(flowid == 0 && !insert_if_full())) {
                VPRINT(2, "ID is 0 and table is full!");
                KILL_PACKET(p, core);
                return;
            }

            VPRINT(3, "Creating id %d on core %i", flowid, core);
        }
        // INSERT IN TABLE
        CYCLES_PROCESS_PRE_INSERT
        ret = insert(fid, flowid, core);
        CYCLES_PROCESS_POST_INSERT

        if (have_maintainer.no_fid) {
            flowid = ret;
        }

        VPRINT(3, "Inserting returned %i for flow %i", ret, flowid);

        if (unlikely(ret == 0)) {
            KILL_PACKET(p, core);
            return;
        }

        fcb = get_fcb(ret, core);
        *(get_flowid(fcb)) = flowid;

        if (_timeout) 
            memcpy(get_fid(fcb), &fid, sizeof(IPFlow5ID));

    } // if ret ==0
    else {
#if IMP_COUNTERS
        _tables[real_processor].matched_packets++;
#endif
        // Old flow
        VPRINT(3, "Old flow %i", ret);
        fcb = get_fcb(ret, core);
    }

    update_lastseen(fcb, recent);
    // fcb->lastseen = recent;

    if (b.last == ret) {
        DBG_PRINT();
        b.append(p);
    } else {
        DBG_PRINT();
        PacketBatch *batch;
        batch = b.finish();
        if (batch) {
            output_push_batch(0, batch);
        }
        fcb_stack = fcb;
        b.init();
        b.append(p);
        b.last = ret;
        if (_cache) b.last_id = fid;
        DBG_PRINT();
    }
    CYCLES_PROCESS_END
}

VFIM_TEMPLATE
uint32_t VFIM_CLASS::get_next(int core) {
    assert(!have_maintainer.no_fid);
    int ret = 0;
    _tables[core].lock->acquire();
    if (likely(!flows_empty(core))) {
        ret = flows_pop(core);
    }
    _tables[core].lock->release();
    return ret;
}

// Flow metadata helpers
VFIM_TEMPLATE
FlowControlBlock *VFIM_CLASS::get_fcb(int i, int core) {
    return (FlowControlBlock *)(((uint8_t *)_tables[core].fcbs) +
                                _flow_state_size_full * i);
}
VFIM_TEMPLATE
void VFIM_CLASS::update_lastseen(FlowControlBlock *fcb, Timestamp &ts) {
    fcb->lastseen = ts;
}

// count is how many elements we have removed from the stack
VFIM_TEMPLATE
int VFIM_CLASS::get_count(int core) {
    assert(!have_maintainer.no_fid);
    return (_table_size - _tables[core].flows_stack_i - 1);
}

VFIM_TEMPLATE
int VFIM_CLASS::get_count() {
    if (is_imp) {
        int sum = 0;
        for (int i = 0; i < _tables_count; i++) {
            if (_tables[i].hash) sum += get_count(i);
        }
        return sum;
    } else
        return get_count(0);
}

VFIM_TEMPLATE double VFIM_CLASS::get_avg_load() {
    return get_count() / (double)_total_size;
}

VFIM_TEMPLATE
int VFIM_CLASS::maintainer(int core) {
    int count = 0;
    FlowControlBlock *fcb;
    int pos;
    int *positions;
    int *flowids;
    Timestamp recent = Timestamp::recent_steady();
    if (have_deferred_free) {
        positions = (int *)malloc(sizeof(int) * _scan_per_epoch);
        flowids = (int *)malloc(sizeof(int) * _scan_per_epoch);
    }
    //  click_chatter("Releasing %d to %d", _tables[core].current,
    //  _scan_per_epoch);
    for (int i = _tables[core].current;
         i < _tables[core].current + _scan_per_epoch; i++) {
        fcb = get_fcb(i, core);

        // If flowid != 0 then we have set the timeout
        if ((*get_flowid(fcb) != 0) &&
            ((recent - fcb->lastseen).msecval() > _timeout_ms)) {
            // VPRINT(0, "[%i/%i/%i]Deleting entry with age %i", count,
            // i,_table_size, recent.msecval() - fcb->lastseen.msecval());

            // Delete is in general safe (if not, must be protected by the table
            // itself)
            pos = delete_flow(fcb, core);

            if (likely(pos > 0)) {
                if (have_deferred_free) // Push for late deletion
                {
                    positions[count] = pos;
                    flowids[count] = *get_flowid(fcb);
                    *get_flowid(fcb) = 0;
                } else {
#if BATCH_FLOWSTACK
                    *get_flowid(fcb) = 0;
                    flows_push(core, *get_flowid(fcb));
#else
                    // Lock and delete now
                    _tables[core].lock->acquire();
                    // In IMP we know the entry is not used, because we're there
                    flows_push(core, *get_flowid(fcb));
                    *get_flowid(fcb) = 0;
                    _tables[core].lock->release();
#endif
                }
                count++;
                // bzero(fcb, sizeof(_flow_state_size_full));
            }
        }
    }

    if (have_deferred_free) {
        if (count > 0) {
            // Lock until we finished to remove entries
            // TODO: This or lock per-each entry that we remove?
#if BATCH_FLOWSTACK
            for (int i = count - 1; i >= 0; i--) {
                free_pos(positions[i], core);
                flows_push(core, flowids[i]);
            }
#else
            _tables[core].lock->acquire();
            for (int i = count - 1; i >= 0; i--) {
                free_pos(positions[i], core);
                flows_push(core, flowids[i]);
            }
            _tables[core].lock->release();
#endif
        }

        free(positions);
        free(flowids);
    }
    _tables[core].current += _scan_per_epoch;
    if (_tables[core].current >= _table_size) _tables[core].current = 0;
    return count;
}

enum {
    h_count,
    h_capacity,
    h_total_capacity,
    h_avg_load
#if IMP_COUNTERS
    ,
    h_cnt_count_inserts,
    h_cnt_count_lookups,
    h_cnt_cycles_inserts,
    h_cnt_cycles_lookups,
    h_killed_packets,
    h_matched_packets,
    h_matched_packets_ratio,
    h_cnt_cycles_total,
    h_maintain_removed
#endif
};

// When enough packets have passed, we compute the
// average for the interval that have just passed
#if IMP_COUNTERS
VFIM_TEMPLATE
void VirtualFlowIPManagerIMP<Lock, is_imp, have_deferred_free,
                             have_maintainer>::update_averages(int core) {
#if HAVE_CYCLES
    _tables[core].avg_cycles_inserts =
        _tables[core].this_inserts
            ? _tables[core].cycles_inserts / _tables[core].this_inserts
            : 0;
    _tables[core].avg_cycles_lookups =
        _tables[core].this_lookups
            ? _tables[core].cycles_lookups / _tables[core].this_lookups
            : 0;

#if IMP_COUNTERS_TOTAL

    _tables[core].avg_cycles_total =
        _tables[core].this_lookups
            ? _tables[core].cycles_total / _tables[core].this_lookups
            : 0;
    _tables[core].cycles_total = 0;
#endif
    _tables[core].cycles_inserts = 0;
    _tables[core].cycles_lookups = 0;
#endif

    _tables[core].count_inserts += _tables[core].this_inserts;
    _tables[core].count_lookups += _tables[core].this_lookups;
    _tables[core].this_inserts = 0;
    _tables[core].this_lookups = 0;
}

VFIM_TEMPLATE
String VFIM_CLASS::get_counter(int cnt) {
    uint64_t s = 0;
    double sf = 0;
    int c = 0;
    for (int i = 0; i < _tables_count; i++)
        if (_tables[i].flows_stack_i != -2) {
            if ((cnt == h_cnt_cycles_inserts || cnt == h_cnt_cycles_lookups ||
                 cnt == h_cnt_cycles_total) &&
                _tables[i].this_lookups > UPDATE_THRESHOLD)
                update_averages(i);
            switch (cnt) {
            case h_cnt_count_inserts:
                s += _tables[i].count_inserts;
                break;
            case h_cnt_count_lookups:
                s += _tables[i].count_lookups;
                break;
            // case CYCLES_INSERTS: s+= _tables[i].cycles_inserts; break;
            // case CYCLES_LOOKUPS: s+= _tables[i].cycles_lookups; break;
            case h_cnt_cycles_inserts:
                s += _tables[i].avg_cycles_inserts;
                break;
            case h_cnt_cycles_lookups:
                s += _tables[i].avg_cycles_lookups;
                break;
#if IMP_COUNTERS_TOTAL
            case h_cnt_cycles_total:
                s += _tables[i].avg_cycles_total;
                break;
#endif
            case h_killed_packets:
                s += _tables[i].killed_packets;
                break;
            case h_matched_packets:
                s += _tables[i].matched_packets;
                break;
            case h_matched_packets_ratio:
                sf += _tables[i].count_lookups
                          ? (_tables[i].matched_packets /
                             (double)_tables[i].count_lookups)
                          : 0;
                break;
            case h_maintain_removed:
                s += _tables[i].maintain_removed;
                break;
            default:
                return "<error>";
                break;
            }
            c += 1;
        }
    if (cnt == h_cnt_cycles_inserts || cnt == h_cnt_cycles_lookups ||
        cnt == h_cnt_cycles_total)
        return String(
            c > 0
                ? s / ((double)c)
                : 0); // If we want an average, than make a average of averages
    else if (cnt == h_matched_packets_ratio)
        return String(c > 0 ? sf / c : 0);
    else
        return String(s);
}

#endif

VFIM_TEMPLATE
int VFIM_CLASS::alloc_fcb(int core) {
    uint64_t nb_bytes = (uint64_t)_flow_state_size_full * (uint64_t)max_key_id(core);
    click_chatter("[%i] FCB table will be %lu Bytes (%f GB)",
           core,
           nb_bytes,
           nb_bytes / (1024 * 1024 * 1024.0d));

    if (!is_imp && core > 0)  {
        return 0;
    }

    _tables[core].fcbs = (FlowControlBlock *)rte_zmalloc(
        "FCBS", nb_bytes,
        CLICK_CACHE_LINE_SIZE);

    if (!_tables[core].fcbs) {
        click_chatter("Error while allocating fcbs table");
        return 1;
    }
    bzero(_tables[core].fcbs, nb_bytes);
    return 0;
}

VFIM_TEMPLATE
String VFIM_CLASS::read_handler(Element *e, void *thunk) {
    VFIM_CLASS *f = static_cast<VFIM_CLASS *>(e);

    intptr_t cnt = (intptr_t)thunk;
    switch (cnt) {
    case h_count:
        return String(f->get_count());
    case h_capacity:
        return String(f->_table_size);
    case h_total_capacity:
        return String(f->_total_size);
    case h_avg_load:
        return String(f->get_avg_load());
#if IMP_COUNTERS
    case h_cnt_cycles_inserts:
    case h_cnt_cycles_lookups:
    case h_cnt_cycles_total:
    case h_cnt_count_inserts:
    case h_cnt_count_lookups:
    case h_killed_packets:
    case h_matched_packets:
    case h_matched_packets_ratio:
    case h_maintain_removed:
        return f->get_counter(cnt);
#endif
    default:
        return "<error>";
    }
}

VFIM_TEMPLATE
int VFIM_CLASS::write_handler(const String &input, Element *e, void *thunk,
                              ErrorHandler *errh) {
    VFIM_CLASS *f = static_cast<VFIM_CLASS *>(e);

    switch ((uintptr_t)thunk) {
    default:
        return 0;
    }
}
VFIM_TEMPLATE void VFIM_CLASS::add_handlers() {
    add_read_handler("count", read_handler, h_count);
    add_read_handler("capacity", read_handler, h_capacity);
    add_read_handler("total_capacity", read_handler, h_total_capacity);
    add_read_handler("avg_load", read_handler, h_avg_load);
#if IMP_COUNTERS
    add_read_handler("inserts", read_handler, h_cnt_count_inserts);
    add_read_handler("lookups", read_handler, h_cnt_count_lookups);
    add_read_handler("inserts_cycles", read_handler, h_cnt_cycles_inserts);
    add_read_handler("lookups_cycles", read_handler, h_cnt_cycles_lookups);
    add_read_handler("total_cycles", read_handler, h_cnt_cycles_total);
    add_read_handler("killed_packets", read_handler, h_killed_packets);
    add_read_handler("matched_packets", read_handler, h_matched_packets);
    add_read_handler("matched_packets_ratio", read_handler,
                     h_matched_packets_ratio);
    add_read_handler("maintain_removed", read_handler, h_maintain_removed);
#endif
}

VFIM_TEMPLATE
int VFIM_CLASS ::table_size_per_thread(int size, int threads) {
    if (is_imp)
        return next_pow2(size / (float)threads);
    else
        return next_pow2(size);
};
VFIM_TEMPLATE
int VFIM_CLASS ::total_table_size(int size, int threads) {
    if (is_imp)
        return size * threads;
    else
        return size;
};



#if BATCH_FLOWSTACK
// These are for MP methods: core is always 0!
VFIM_TEMPLATE
int VFIM_CLASS ::local_flow_stacks_init() {
    if(!is_imp)
    {
	    auto passing = get_passing_threads();

        _tables[0].local_flowstacks_i= (int*) rte_zmalloc("LOCALSTACKS_I", sizeof(int) * passing.size(), CLICK_CACHE_LINE_SIZE);
        _tables[0].local_flowstacks= (uint32_t **) rte_zmalloc("LOCALSTACKS", sizeof(uint32_t *) * passing.size(), CLICK_CACHE_LINE_SIZE);
        assert(_tables[0].local_flowstacks_i != NULL);
        assert(_tables[0].local_flowstacks != NULL);

        _flows_total =  _flows_batchsize * 2;
        _flows_hithresh = _flows_batchsize * 1.5;
        _flows_lothresh = _flows_batchsize * .5;

        for(int core=0; core<passing.size(); core++)
        {
            if(passing[core])
            {
                click_chatter("[%d] Initializing local flow stack", core);
            _tables[0].local_flowstacks[core] = (uint32_t *) rte_zmalloc("STACK", sizeof(uint32_t) * _flows_total, CLICK_CACHE_LINE_SIZE);
            assert(_tables[0].local_flowstacks[core] != NULL);
            }
        }

        return 0;
    }
    // print_stack(0);
    return 0;
}

VFIM_TEMPLATE
int VFIM_CLASS:: get_flowbatch(int core)
{
    // click_chatter("%s[%i]", __func__, core);
    assert(! is_imp);

    int localcore = click_current_cpu_id();
    _tables[core].lock->acquire(); 
    int howmany = min(_flows_batchsize - _tables[core].local_flowstacks_i[localcore] -1, _tables[core].flows_stack_i);
    if(howmany>0)
    {
	for(int i=0; i< howmany; i++)
	{
	    int flow = _tables[core].flows_stack[_tables[core].flows_stack_i--];
	    _tables[core].local_flowstacks[localcore][++_tables[core].local_flowstacks_i[localcore]] = flow;
	}

    }
    _tables[core].lock->release(); 
    // print_stack(0);
    return howmany;

}

VFIM_TEMPLATE
inline bool VFIM_CLASS ::local_flows_empty(int core, int localcore) {
    return _tables[core].local_flowstacks_i[localcore] <= 0;
}
VFIM_TEMPLATE
inline void VFIM_CLASS ::local_flows_push(int core, int localcore, uint32_t flow) {
    // click_chatter("%s[%i,%i] at index %i", __func__, localcore, flow,
	// 1+_tables[core].local_flowstacks_i[localcore]);
    _tables[core].local_flowstacks[localcore][++_tables[core].local_flowstacks_i[localcore]] = flow;
}
VFIM_TEMPLATE
inline uint32_t VFIM_CLASS ::local_flows_pop(int core, int localcore) {
    // click_chatter("%s[%i] at index %i flow is %i", __func__, localcore,
    // _tables[core].local_flowstacks_i[localcore],
    // _tables[core].local_flowstacks[localcore][_tables[core].local_flowstacks_i[localcore]]);
    return _tables[core].local_flowstacks[localcore][_tables[core].local_flowstacks_i[localcore]--];
}

VFIM_TEMPLATE
uint32_t VFIM_CLASS ::flows_pop(int core) {
    // print_stack(0);
    if(is_imp)
	return imp_flows_pop(core);

    int localcore = click_current_cpu_id();
    bool acquired = false;

    if ( _tables[core].local_flowstacks_i[localcore] < _flows_lothresh)
    {
	// click_chatter("%s we are below threshold", __func__);
	if ( _tables[core].local_flowstacks_i[localcore] <= 0)
	    {
		//	click_chatter("%s we are empty!. acquire", __func__);
		_tables[core].lock->acquire();
		acquired = true;
	    }
	else
	{
	    // click_chatter("%s attempt to take lock", __func__);
	    acquired = _tables[core].lock->attempt();
	    // click_chatter("%s Got it? %i", __func__, acquired);
	}

	if(acquired)
	{
	    if (get_flowbatch(core) == 0)
		return 0;
	    // click_chatter("%s get_flowbatch was ok!", __func__);
	    _tables[core].lock->release();
	}
    }

    //  click_chatter("%s local pop!", __func__);
    return local_flows_pop(core, localcore);

}
VFIM_TEMPLATE
void VFIM_CLASS ::flows_push(int core, uint32_t flow, const bool init) {
    //click_chatter("[%d] %s", core, __func__);
    // print_stack(0);
    if (is_imp || unlikely(init))
    {
        _tables[core].lock->acquire();
        imp_flows_push(core, flow);
        _tables[core].lock->release();
    }
    else
    {

    int localcore= click_current_cpu_id();
    bool acquired = false;

    // If we are in the upper quarter, check if we can push without locking.
    // If we are full, force lock
    // If we are in the bottom half, do not push to global stack
    
    // click_chatter("Local stack is %i, is it higher than hitresh(%i)? %i",
    // _tables[core].local_flowstacks_i[localcore], _flows_hithresh,
    // ( _tables[core].local_flowstacks_i[localcore] > _flows_hithresh));
	
    if ( _tables[core].local_flowstacks_i[localcore] >= _flows_total -1)
    {
	//click_chatter("%s force acquire", __func__);
	_tables[core].lock->acquire();
	acquired = true;
    }
    else
    {
	if( _tables[core].local_flowstacks_i[localcore] > _flows_hithresh)
	    acquired = _tables[core].lock->attempt();
    }
    if (acquired)
    {
	// Push a batch of flows to the global stack
	for(; _tables[core].local_flowstacks_i[localcore] > _flows_batchsize;)
	{
	    // click_chatter("Pushing a global flow... local i is %i, global i %i, batchsize %i",
		// _tables[core].local_flowstacks_i[localcore],
		// _tables[core].flows_stack_i,
		// _flows_batchsize);
	    int global_i = ++_tables[core].flows_stack_i;
	    int local_i = _tables[core].local_flowstacks_i[localcore]--;
	    _tables[core].flows_stack[global_i] = _tables[core].local_flowstacks[localcore][local_i];


	    // _tables[core].flows_stack[++_tables[core].flows_stack_i] = 
		// _tables[core].local_flowstacks[localcore][--_tables[core].local_flowstacks_i[localcore]];
	}
	_tables[core].lock->release();
    }

	// We should always have space here... 
	// click_chatter("Now we push the flow to the local stack");
	if (likely(_tables[core].local_flowstacks_i[localcore] <= _flows_total))
	{
	    int local_i = ++_tables[core].local_flowstacks_i[localcore];
	    // click_chatter("Pushing %i to local index %i", flow, local_i);
	    _tables[core].local_flowstacks[localcore][local_i] = flow;
	}
	
    }
}
VFIM_TEMPLATE
inline bool VFIM_CLASS ::flows_empty(int core) {
    if(is_imp)
	return imp_flows_empty(core);
    int lcore = click_current_cpu_id();
    return unlikely(_tables[core].flows_stack_i < 0) && _tables[core].local_flowstacks_i[lcore] <= 0;
}

VFIM_TEMPLATE
inline bool VFIM_CLASS ::is_full(int core) { return flows_empty(core); }

//Helper method to print flow stack
VFIM_TEMPLATE void VFIM_CLASS::print_stack(int core) {
    char s[10000];
    sprintf(s, "------------------\ni is %i. Empty? %i\n",
            _tables[core].flows_stack_i, flows_empty(core));
    if (_table_size < 1000) {
        for (int i = 0; i < _table_size; i++)
            sprintf(s, "%s%02i, ", s, _tables[core].flows_stack[i]);
        sprintf(s, "%s\n", s);
        for (int i = 0; i < _tables[core].flows_stack_i; i++)
            sprintf(s, "%s----", s);
        sprintf(s, "%s^", s);
    }
    auto passing = get_passing_threads();
    for(int lcore=0; lcore<passing.size(); lcore++)
    {
	if(passing[lcore])
	{
	sprintf(s,"%s\nCORE %i [%i]:\n",s, lcore, _tables[core].local_flowstacks_i[lcore] );
        for (int i = 0; i < _flows_total; i++)
            sprintf(s, "%s%02i, ", s, _tables[core].local_flowstacks[lcore][i]);
        sprintf(s, "%s\n", s);
        for (int i = 0; i < _tables[core].local_flowstacks_i[lcore]; i++)
            sprintf(s, "%s----", s);
        sprintf(s, "%s^", s);
	}
	
    }



    click_chatter("%s", s);
}




VFIM_TEMPLATE
uint32_t VFIM_CLASS ::imp_flows_pop(int core) {
    // WARNING! flows_pop has no control, check flows_empty before!
    // click_chatter("%s[%i]: returning %i at index %i",
	    // __func__, core,
	    // _tables[core].flows_stack[_tables[core].flows_stack_i],
	    // _tables[core].flows_stack_i);

    return _tables[core].flows_stack[_tables[core].flows_stack_i--];
}
VFIM_TEMPLATE
void VFIM_CLASS ::imp_flows_push(int core, uint32_t flow) {
    // click_chatter("%s[%i]: pushing %i at index %i",
	    // __func__, core,
	    // flow,
	    // _tables[core].flows_stack_i+1);
    _tables[core].flows_stack[++_tables[core].flows_stack_i] = flow;
}
VFIM_TEMPLATE
inline bool VFIM_CLASS ::imp_flows_empty(int core) {
    return _tables[core].flows_stack_i < 0;
}
#else


VFIM_TEMPLATE
uint32_t VFIM_CLASS ::flows_pop(int core) {
    // WARNING! flows_pop has no control, check flows_empty before!
    return _tables[core].flows_stack[_tables[core].flows_stack_i--];
}
VFIM_TEMPLATE
void VFIM_CLASS ::flows_push(int core, uint32_t flow, bool init) {
    _tables[core].flows_stack[++_tables[core].flows_stack_i] = flow;
}
VFIM_TEMPLATE
inline bool VFIM_CLASS ::flows_empty(int core) {
    return _tables[core].flows_stack_i < 0;
}

VFIM_TEMPLATE
inline bool VFIM_CLASS ::is_full(int core) { return flows_empty(core); }

// Helper method to print flow stack
VFIM_TEMPLATE void VFIM_CLASS::print_stack(int core) {
    char s[10000];
    sprintf(s, "------------------\ni is %i. Empty? %i\n",
            _tables[core].flows_stack_i, flows_empty(core));
    if (_table_size < 1000) {
        for (int i = 0; i < _table_size; i++)
            sprintf(s, "%s%02i, ", s, _tables[core].flows_stack[i]);
        sprintf(s, "%s\n", s);
        for (int i = 0; i < _tables[core].flows_stack_i; i++)
            sprintf(s, "%s----", s);
        sprintf(s, "%s^", s);
    }
    click_chatter("%s", s);
}


#endif

CLICK_ENDDECLS
#endif
