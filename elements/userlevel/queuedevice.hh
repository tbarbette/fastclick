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

    static void static_initialize();

private :
    /* Those two are only used during configurations. On runtime, the final
     * n_queues choice is used.*/
    int _minqueues;
    int _maxqueues;
    friend RXQueueDevice;
    friend TXQueueDevice;
protected:

	int _burst; //Max size of burst
    Vector<Task*> _tasks;
    Vector<atomic_uint32_t> _locks;
#define NO_LOCK 2

    Bitvector usable_threads;
    int queue_per_threads;
    int queue_share;
    unsigned ndesc;
    int _verbose;
    bool allow_nonexistent;

    int _maxthreads;
    int firstqueue;
    int lastqueue;

    // n_queues will be the final choice in [_minqueues, _maxqueues].
    int n_queues;

    int thread_share;

    Vector<int> _thread_to_firstqueue;
    Vector<int> _queue_to_thread;

    static int n_initialized; //Number of total elements configured
    static int n_elements; //Number of total elements heriting from QueueDevice
    static int n_inputs; //Number of total input
    static int use_nodes; //Number of numa nodes used

    static Vector<int> inputs_count; //Number of inputs per numa node

    static Vector<int> shared_offset; //Thread offset for each node

    class ThreadState {
        public:
        ThreadState() : _count(0), _dropped(0) {};
        long long unsigned _count;
        long long unsigned _dropped;
    };
    per_thread<ThreadState> thread_state;

    int _this_node; //Numa node index

    bool _active;

    inline bool lock_attempt() {
        if (_locks[id_for_thread()] != NO_LOCK) {
            if (_locks[id_for_thread()].swap((uint32_t)1) == (uint32_t)0)
                return true;
            else
                return false;
        }
        else {
            return true;
        }
    }

    inline void lock() {
        if (_locks[id_for_thread()] != NO_LOCK) {
            while ( _locks[id_for_thread()].swap((uint32_t)1) != (uint32_t)0)
                    do {
                    click_relax_fence();
                    } while ( _locks[id_for_thread()] != (uint32_t)0);
        }
    }

    inline void unlock() {
        if (_locks[id_for_thread()] != NO_LOCK)
            _locks[id_for_thread()] = (uint32_t)0;
    }

    enum {h_count};

    unsigned long long n_count();
    unsigned long long n_dropped();
    void reset_count();
    static String count_handler(Element *e, void *user_data);
    static String dropped_handler(Element *e, void *);

    static int reset_count_handler(const String &, Element *e, void *,
                                    ErrorHandler *);

    inline void add_count(unsigned int n) {
        thread_state->_count += n;
    }

    inline void set_dropped(long long unsigned n) {
        thread_state->_dropped = n;
    }

    inline void add_dropped(unsigned int n) {
        thread_state->_dropped += n;
    }

    bool get_spawning_threads(Bitvector& bmk, bool isoutput, int port);

    /**
     * Common parsing for all kind of QueueDevice
     */
    int parse(Vector<String> &conf, ErrorHandler *errh);


    bool all_initialized() {
        return n_elements == n_initialized;
    }

    int initialize_tasks(bool schedule, ErrorHandler *errh);

    void cleanup_tasks();

    inline int queue_for_thread_begin(int tid) {
        return _thread_to_firstqueue[tid];
    }

    inline int queue_for_thread_end(int tid) {
        int q =  _thread_to_firstqueue[tid] + queue_per_threads - 1;
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

    inline int id_for_thread(int tid) {
        if (likely(queue_per_threads == 1))
            return _thread_to_firstqueue[tid] - firstqueue;
        else
            return (_thread_to_firstqueue[tid] - firstqueue) / queue_per_threads;
    }

    inline int id_for_thread() {
        return id_for_thread(click_current_cpu_id());
    }

    inline Task* task_for_thread() {
        return _tasks[id_for_thread()];
    }

    inline Task* task_for_thread(int tid) {
        return _tasks[id_for_thread(tid)];
    }

    inline bool thread_for_queue_available() {
        return !_queue_to_thread.empty();
    }

    inline int thread_for_queue(int queue) {
        return _queue_to_thread[queue];
    }

    int thread_per_queues() {
        return queue_share;
    }
};


class RXQueueDevice : public QueueDevice {
protected:
	bool _promisc;
	bool _set_rss_aggregate;
	bool _set_paint_anno;
	int _threadoffset;
	bool _use_numa;
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
