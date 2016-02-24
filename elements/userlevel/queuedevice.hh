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
#if HAVE_NUMA
#include <click/numa.hh>
#endif

class QueueDevice : public BatchElement {

public:

    QueueDevice() CLICK_COLD;

    static void static_initialize();

protected:
    int nqueues;//Number of queues in the device

    Vector<Task*> _tasks;
    Vector<atomic_uint32_t> _locks;

#define NO_LOCK 2
    Bitvector usable_threads;
    int queue_per_threads;
    int queue_share;
    unsigned ndesc;
    bool _numa;
    bool _verbose;
private :
    int _maxthreads;
    int _minqueues;
    int _maxqueues;
    int _threadoffset;

    int thread_share;

    Vector<int> _thread_to_queue;
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

protected:

    int _this_node; //Numa node index

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

    inline unsigned long long n_count() {
        unsigned long long total = 0;
        for (unsigned int i = 0; i < thread_state.size(); i ++) {
            total += thread_state.get_value(i)._count;
        }
        return total;
    }

    inline unsigned long long n_dropped() {
        unsigned long long total = 0;
        for (unsigned int i = 0; i < thread_state.size(); i ++) {
            total += thread_state.get_value(i)._dropped;
        }
        return total;
    }

    inline void reset_count() {
        for (unsigned int i = 0; i < thread_state.size(); i ++) {
            thread_state.get_value(i)._count = 0;
            thread_state.get_value(i)._dropped = 0;
        }
    }

    static String count_handler(Element *e, void *)
    {
        QueueDevice *tdd = static_cast<QueueDevice *>(e);
        return String(tdd->n_count());
    }

    static String dropped_handler(Element *e, void *)
        {
            QueueDevice *tdd = static_cast<QueueDevice *>(e);
            return String(tdd->n_dropped());
        }

    static int reset_count_handler(const String &, Element *e, void *,
                                    ErrorHandler *)
    {
        QueueDevice *tdd = static_cast<QueueDevice *>(e);
        tdd->reset_count();
        return 0;
    }

    inline void add_count(unsigned int n) {
        thread_state->_count += n;
    }

    inline void set_dropped(long long unsigned n) {
        thread_state->_dropped = n;
    }

    inline void add_dropped(unsigned int n) {
        thread_state->_dropped += n;
    }

    bool get_runnable_threads(Bitvector& bmk)
    {
    	if (noutputs()) { //RX
    		for (int i = 0; i < nqueues; i++) {
    			for (int j = 0; j < queue_share; j++) {
    				bmk[thread_for_queue(i) - j] = 1;
    			}
    		}
    		return true;
    	} else { //TX
    		if (input_is_pull(0)) {
    			bmk[router()->home_thread_id(this)] = 1;
    		}
    		return true;
    	}
    }

    /*
     * Configure a RX side of a queuedevice. Take cares of setting user max
     *  threads, queues and offset and registering this rx device for later
     *  sharing.
     * numa should be set.
     */
    int configure_rx(int numa_node,unsigned int maxthreads, unsigned int minqueues, unsigned int maxqueues, unsigned int threadoffset, ErrorHandler *errh);
    int configure_tx(unsigned int maxthreads,unsigned int minqueues, unsigned int maxqueues, ErrorHandler *errh);
    int initialize_tx(ErrorHandler *errh);
    int initialize_rx(ErrorHandler *errh);

    bool all_initialized() {
        return n_elements == n_initialized;
    }

    int initialize_tasks(bool schedule, ErrorHandler *errh) {
        _tasks.resize(usable_threads.weight());
        _locks.resize(usable_threads.weight());
        _thread_to_queue.resize(master()->nthreads());
        _queue_to_thread.resize(nqueues);

        int th_num = 0;

        int share_idx = 0;
        for (int th_id = 0; th_id < master()->nthreads(); th_id++) {

            if (!usable_threads[th_id]) {
                continue;
            }
            if (share_idx % thread_share != 0) {
                --th_num;
                if (_locks[th_num] == NO_LOCK) {
                    _locks[th_num] = 0;
                }
            } else {
                _tasks[th_num] = (new Task(this));
                ScheduleInfo::initialize_task(this, _tasks[th_num], schedule, errh);
                _tasks[th_num]->move_thread(th_id);
                _locks[th_num] = NO_LOCK;
            }
            share_idx++;

            _thread_to_queue[th_id] = (th_num * queue_per_threads) / queue_share;

            for (int j = 0; j < queue_per_threads; j++) {
                if (_verbose)
                    click_chatter("Queue %d handled by th %d",((th_num * queue_per_threads) + j) / queue_share,th_id);
                _queue_to_thread[((th_num * queue_per_threads) + j) / queue_share] = th_id;
            }

            if (queue_share > 1) {
                _locks[th_num] = 0;
            }

            ++th_num;

        }

        return 0;

    }

    void cleanup_tasks() {
        for (int i = 0; i < usable_threads.weight(); i++) {
            if (_tasks[i])
                delete _tasks[i];
        }
    }

    inline int queue_for_thread_begin() {
        return _thread_to_queue[click_current_cpu_id()];
    }

    inline int queue_for_thread_end() {
        return _thread_to_queue[click_current_cpu_id()] + queue_per_threads - 1;
    }

    inline int id_for_thread(int tid) {
        return _thread_to_queue[tid] / queue_per_threads;
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

    inline int thread_for_queue(int queue) {
        return _queue_to_thread[queue];
    }
};

CLICK_ENDDECLS
#endif
