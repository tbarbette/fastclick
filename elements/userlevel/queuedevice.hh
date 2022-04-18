#ifndef CLICK_QUEUEDEVICE_HH
#define CLICK_QUEUEDEVICE_HH

#include <click/error.hh>
#include <click/batchelement.hh>
#include <click/vector.hh>
#include <click/bitvector.hh>
#include <click/sync.hh>
#include <click/master.hh>
#include <click/multithread.hh>
#include <click/standard/scheduleinfo.hh>
#include <click/args.hh>
#if HAVE_NUMA
#include <click/numa.hh>
#endif

class RXQueueDevice;
class TXQueueDevice;

class QueueDevice : public BatchElement {

public:

    QueueDevice() CLICK_COLD;

    const char *class_name() const		{ return "QueueDevice"; }

    static void static_initialize();

    unsigned get_nb_desc() {
        return ndesc;
    }
private :
    /* Those two are only used during configurations. On runtime, the final
     * n_queues choice is used.*/
    int _minqueues;
    int _maxqueues;

    friend RXQueueDevice;
    friend TXQueueDevice;
protected:



    #define NO_LOCK 2
    /**
     * Per-queue data structure. Have a lock per queue, when multiple thread
     * can access the same queue, and a reference to a thread id serving this
     * queue.
     */
    struct QueueInfo {
        QueueInfo() : thread_id(-1) {
            lock = NO_LOCK;
        }
        atomic_uint32_t lock;
        unsigned thread_id;
    } CLICK_CACHE_ALIGN;

	int _burst; //Max size of burst

    Vector<QueueInfo,CLICK_CACHE_LINE_SIZE> _q_infos;

    Bitvector usable_threads;
    int queue_per_threads;
    int queue_share;
    unsigned ndesc;
    bool allow_nonexistent;

    int _maxthreads;
    int _minthreads;
    int firstqueue;
    int lastqueue;

    // n_queues will be the final choice in [_minqueues, _maxqueues].
    int n_queues;

    //Number of queues per threads, normally 1
    int thread_share;

    /**
     * Per-thread state. Holds statistics, pointer to the per-thread task and id of the first queue
     * to be served by this thread
     */
    class ThreadState {
        public:
        ThreadState() : _count(0), _dropped(0), task(0), first_queue_id(-1) {};
        long long unsigned _count;
        long long unsigned _dropped;
        Task*       task;
        unsigned    first_queue_id;
    };

    per_thread<ThreadState> _thread_state;

    int _this_node; // Numa node index

    bool _active; // Is this element active

    //Verbosity level
    int _verbose;

    static int n_initialized; //Number of total elements configured
    static int n_elements; //Number of total elements heriting from QueueDevice
    static int n_inputs; //Number of total input
    static int use_nodes; //Number of numa nodes used

    static Vector<int> inputs_count; //Number of inputs per numa node

    static Vector<int> shared_offset; //Thread offset for each node

    /**
     * Attempt to take the per-queue lock
     * @return true if taken
     */
    inline bool lock_attempt() {
        if (_q_infos[id_for_thread()].lock.nonatomic_value() != NO_LOCK) {
            if (_q_infos[id_for_thread()].lock.swap((uint32_t)1) == (uint32_t)0)
                return true;
            else
                return false;
        }
        else {
            return true;
        }
    }

    /**
     * Takes the per-queue lock
     */
    inline void lock() {
        if (_q_infos[id_for_thread()].lock.nonatomic_value() != NO_LOCK) {
            while (_q_infos[id_for_thread()].lock.swap((uint32_t)1) != (uint32_t)0)
                    do {
                    click_relax_fence();
                    } while ( _q_infos[id_for_thread()].lock != (uint32_t)0);
        }
    }

    /**
     * Release the per-queue lock
     */
    inline void unlock() {
        if (_q_infos[id_for_thread()].lock.nonatomic_value() != NO_LOCK) {
            _q_infos[id_for_thread()].lock = (uint32_t)0;
        }
    }

    enum {h_count,h_useful,h_useless};

    unsigned long long n_count();
    unsigned long long n_dropped();
    void reset_count();
    static String count_handler(Element *e, void *user_data);
    static String dropped_handler(Element *e, void *);

    static int reset_count_handler(const String &, Element *e, void *,
                                    ErrorHandler *);

    inline void add_count(unsigned int n) {
        _thread_state->_count += n;
    }

    inline void set_dropped(long long unsigned n) {
        _thread_state->_dropped = n;
    }

    inline void add_dropped(unsigned int n) {
        _thread_state->_dropped += n;
    }

    bool get_spawning_threads(Bitvector& bmk, bool isoutput, int port);

    /**
     * Common parsing for all kind of QueueDevice
     */
    int parse(Vector<String> &conf, ErrorHandler *errh);


    bool all_initialized() {
        return n_elements == n_initialized;
    }

    int initialize_tasks(bool schedule, ErrorHandler *errh, TaskCallback hook = 0);


    void cleanup_tasks();

    inline int queue_for_thread_begin(int tid) {
        return _thread_state.get_value_for_thread(tid).first_queue_id;
    }

    inline int queue_for_thread_end(int tid) {
        int q =  _thread_state.get_value_for_thread(tid).first_queue_id + queue_per_threads - 1;
        if (unlikely(q > lastqueue))
            return lastqueue;
        return q;

    }

    inline int queue_for_thisthread_begin() {
        return queue_for_thread_begin(click_current_cpu_id());
    }

    inline int queue_for_thisthread_end() {
        return queue_for_thread_end(click_current_cpu_id());
    }

    /**
     * Return the queue index for a given thread
     */
    inline int id_for_thread(int tid) {
        if (likely(queue_per_threads == 1))
            return queue_for_thread_begin(tid) - firstqueue;
        else
            return (queue_for_thread_begin(tid) - firstqueue) / queue_per_threads;
    }

    /**
     * Returns the queue id for this thread
     */
    inline int id_for_thread() {
        return id_for_thread(click_current_cpu_id());
    }

    /**
     * Return the task responsible for the current thread
     */
    inline Task* task_for_thread() {
        return _thread_state->task;
    }

    /**
     * Returns the task for a thread
     */
    inline Task* task_for_thread(int tid) {
        return _thread_state.get_value_for_thread(tid).task;
    }

    /**
     * Returns the thread managing a given queue, given by its index (not accountig for first_queue, hence the queue offset)
     */
    inline int thread_for_queue_offset(int queue) {
        return _q_infos[queue].thread_id;
    }

    /**
     * Returns the number of threads per queues, in general 1 except when the number of queues is limited
     */
    int thread_per_queues() {
        return queue_share;
    }
};


class RXQueueDevice : public QueueDevice {
protected:
    bool _promisc;
    bool _vlan_filter;
    bool _vlan_strip;
    bool _vlan_extend;
    bool _lro;
    bool _jumbo;
    bool _set_rss_aggregate;
    bool _set_paint_anno;
    int _threadoffset;
    bool _use_numa;
    int _numa_node_override;
    bool _scale_parallel;

    /**
     * Common parsing for all RXQueueDevice
     */
    int parse(Vector<String> &conf, ErrorHandler *errh);

    /*
     * Configure a RX side of a queuedevice. Take cares of setting user max
     *  threads, queues and offset and registering this rx device for later
     *  sharing.
     * numa should be set.
     */
    int configure_rx(int numa_node, int minqueues, int maxqueues, ErrorHandler *errh);
    int initialize_rx(ErrorHandler *errh);

};

class TXQueueDevice : public QueueDevice {
protected:
    TXQueueDevice();

    bool _blocking;
    int _internal_tx_queue_size;

    /**
     * Common parsing for all RXQueueDevice
     */
    int parse(Vector<String> &conf, ErrorHandler *errh);

    int configure_tx(int hardminqueues, int hardmaxqueues, ErrorHandler *errh);
    int initialize_tx(ErrorHandler *errh);
};

CLICK_ENDDECLS
#endif
