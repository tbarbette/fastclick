// -*- c-basic-offset: 4 -*-
#ifndef CLICK_SYNC_HH
#define CLICK_SYNC_HH
#include <click/machine.hh>
#include <click/glue.hh>
#include <click/atomic.hh>
#if CLICK_LINUXMODULE || (CLICK_USERLEVEL && HAVE_MULTITHREAD)
# define CLICK_MULTITHREAD_SPINLOCK 1
#endif
#if CLICK_USERLEVEL && !NDEBUG
# define SPINLOCK_ASSERTLEVEL "<-999>"
#else
# define SPINLOCK_ASSERTLEVEL "<1>"
#endif
CLICK_DECLS


/**
 * Duplicate a variable per-thread, allowing all threads to have their own
 * version of a variable. As opposed to __thread, any thread can also access
 * other thread's values using get/set_value_for_thread(), allowing proper
 * initialization and cleaning from the only thread calling elements
 * initialize and cleanup functions using a for loop from 0 to size().
 *
 * The class provides convenient functions, eg :
 * per_thread<int> my_int;
 *
 * my_int++;
 * my_int = my_int + 1;
 * int &a = *my_int; //Return a reference
 * a++; //Affect the per-thread variable
 *
 * per_thread<struct foo> my_foo;
 * my_foo->bar++;
 *
 * IMPORTANT:
 * The variable will be cached-align to avoid false sharing. This means that
 * the amount of bytes for each thread will be rounded up to 64 bytes. That
 * means that you should make per_thread of big structure of variables instead
 * of multiple per_threads.
 *
 * In other term, prefer to do :
 * struct state {
 *     int count;
 *     int drop;
 *     ...
 * }
 * per_thread<struct state> _state;
 *
 * Instead of : //this is the bad version
 * per_thread<int> _count; //do not copy-cut
 * per_thread<int> _drop; //do not do this
 * ...
 */
template <typename T>
class per_thread
{
private:
    struct A_t{
        T v;
    } CLICK_CACHE_ALIGN;

    typedef struct A_t AT;

    void initialize(unsigned int n, T v) {
        _size = n;
        storage = CLICK_ALIGNED_NEW(AT,_size);
        for (unsigned i = 0; i < n; i++) {
            storage[i].v = v;
        }
    }

    //Disable copy constructor. It will always be a user error
    per_thread (const per_thread<T> &);
public:
    explicit per_thread() {
        _size = click_max_cpu_ids();
        storage = CLICK_ALIGNED_NEW(AT,_size);
    }

    explicit per_thread(T v) {
        initialize(click_max_cpu_ids(),v);
    }

    explicit per_thread(T v, int n) {
        initialize(n,v);
    }

    /**
     * Resize must be called if per_thread was initialized before
     * click_max_cpu_ids() is set (such as in static functions)
     * This will destroy all data
     */
    void resize(unsigned int max_cpu_id, T v) {
        CLICK_ALIGNED_DELETE(storage, AT, _size);
        initialize(max_cpu_id,v);
    }

    ~per_thread() {
        if (_size) {
            CLICK_ALIGNED_DELETE(storage,AT,_size);
            _size = 0;
        }
    }

    inline T* operator->() const {
        return &(storage[click_current_cpu_id()].v);
    }
    inline T& operator*() const {
        return storage[click_current_cpu_id()].v;
    }

    inline T& operator+=(const T& add) const {
        storage[click_current_cpu_id()].v += add;
        return storage[click_current_cpu_id()].v;
    }

    inline T& operator++() const { // prefix ++
        return ++(storage[click_current_cpu_id()].v);
    }

    inline T operator++(int) const { // postfix ++
        return storage[click_current_cpu_id()].v++;
    }

    inline T& operator--() const {
        return --(storage[click_current_cpu_id()].v);
    }

    inline T operator--(int) const {
        return storage[click_current_cpu_id()].v--;
    }

    inline T& operator=(T value) const {
        storage[click_current_cpu_id()].v = value;
        return storage[click_current_cpu_id()].v;
    }

    inline void operator=(const per_thread<T>& pt) {
        if (_size != pt._size) {
            if (storage)
               CLICK_ALIGNED_DELETE(storage,AT, _size);
            storage = CLICK_ALIGNED_NEW(AT, _size);
        }
        for (int i = 0; i < pt.weight(); i++) {
            storage[i] = pt.storage[i];
        }
    }

    inline void set(T v) {
        storage[click_current_cpu_id()].v = v;
    }

    inline void setAll(T v) {
        for (int i = 0; i < click_max_cpu_ids(); i++)
            storage[i].v = v;
    }

    inline T& get() const{
        return storage[click_current_cpu_id()].v;
    }

    inline const T& cst() const{
        return storage[click_current_cpu_id()].v;
    }

    /**
     * get_value_for_thread get the value for a given thread id
     * Do not do for (int i = 0; i < size(); i++) get_value_for_thread
     * as in omem and oread version, not all threads are represented,
     * this would lead to a bug !
     */
    inline T& get_value_for_thread(int thread_id) const{
        return storage[thread_id].v;
    }

    inline void set_value_for_thread(int thread_id, T v) {
        storage[thread_id].v = v;
    }

    /**
     * get_value can be used to iterate around all per-thread variables.
     * On the normal version it is the same as get_value_for_thread, but on
     * omem and oread version of per_thread, this iterate only over thread
     * that will really be used.
     */
    inline T& get_value(int i) const {
        return get_value_for_thread(i);
    }

    inline void set_value(int i, T v) {
        set_value_for_thread(i, v);
    }

    /**
     * Number of elements inside the vector.
     * This may not the number of threads, only use this
     * to iterate with get_value()/set_value(), not get_value_for_thread()
     */
    inline unsigned weight() const {
        return _size;
    }

    inline unsigned int get_mapping(int i) {
        return i;
    }

    class const_iterator {public:
        const_iterator(const per_thread<T>* t,unsigned pos) {
            _t = t;
            _pos = pos;
        }
        const per_thread<T>* _t;
        unsigned _pos;
        T& operator*() const {
            return _t->get_value(_pos);
        }
        bool operator!=(const const_iterator &o) const {
            return _pos != o._pos;
        }
        void operator++() {
            _pos++;
        }
    };

    const_iterator begin() const {
        return const_iterator(this,0);
    }

    const_iterator end() const {
        return const_iterator(this,_size);
    }

    class iterator {public:
        iterator(per_thread<T>* t,unsigned pos) {
            _t = t;
            _pos = pos;
        }
        const per_thread<T>* _t;
        unsigned _pos;
        T& operator*() const {
            return _t->get_value(_pos);
        }
        bool operator!=(const iterator &o) const {
            return _pos != o._pos;
        }
        void operator++() {
            _pos++;
        }
    };

    iterator begin() {
        return iterator(this,0);
    }

    iterator end() {
        return iterator(this,_size);
    }
protected:
    AT* storage;
    unsigned _size;
};

/**
 * A not per_thread per_thread, usefull to use as replacement in template
 * for non-thread safe of some elements
 */
template <typename T>
class not_per_thread
{
public:
    explicit not_per_thread() {
    }

    explicit not_per_thread(T _v) {
        v = _v;
    }

    inline T* operator->() {
        return &(v);
    }


    inline T& operator*() {
        return v;
    }

    inline T& get() {
        return v;
    }

    inline const T& cst() const {
        return v;
    }

    inline constexpr int weight() const {
        return 1;
    }

    inline T& get_value(int) {
        return v;
    }

    class const_iterator {public:
        const_iterator(const not_per_thread<T>* t,unsigned pos) {
            _t = t;
            _pos = pos;
        }
        const not_per_thread<T>* _t;
        unsigned _pos;
        const T& operator*() const {
            return _t->v;
        }
        bool operator!=(const const_iterator &o) const {
            return _pos != o._pos;
        }
        void operator++() {
            _pos++;
        }
    };

    const_iterator begin() const {
        return const_iterator(this,0);
    }

    const_iterator end() const {
        return const_iterator(this, 1);
    }

    class iterator {public:
        iterator(not_per_thread<T>* t,unsigned pos) {
            _t = t;
            _pos = pos;
        }
        not_per_thread<T>* _t;
        unsigned _pos;
        T& operator*() const {
            return _t->v;
        }
        bool operator!=(const iterator &o) const {
            return _pos != o._pos;
        }
        void operator++() {
            _pos++;
        }
    };

    iterator begin() {
        return iterator(this,0);
    }

    iterator end() {
        return iterator(this, 1);
    }
private:
    T v;
};


#define PER_THREAD_SET(pt,value) \
    for (unsigned i = 0; i < pt.weight(); i++) { \
        pt.set_value(i, value); \
    }

#define PER_THREAD_POINTER_SET(pt,member,value) \
    for (unsigned i = 0; i < pt.weight(); i++) { \
        pt.get_value(i)->member = value; \
    }

#define PER_THREAD_MEMBER_SET(pt,member,value) \
    for (unsigned i = 0; i < pt.weight(); i++) { \
        pt.get_value(i).member = value; \
    }

#define PER_THREAD_VECTOR_SET(pt,member,value) \
    for (unsigned i = 0; i < pt.weight(); i++) { \
        for (int j = 0; j < pt.get_value(i).size(); j++) \
            (pt.get_value(i))[j].member = value; \
    }

#define PER_THREAD_SUM(type, var, pt) \
    type var = 0; \
    for (unsigned i = 0; i < pt.weight(); i++) { \
        var += pt.get_value(i); \
    }

#define PER_THREAD_POINTER_SUM(type, var, pt, member) \
    type var = 0; \
    for (unsigned i = 0; i < pt.weight(); i++) { \
        var += pt.get_value(i)->member; \
    }

#define PER_THREAD_MEMBER_SUM(type, var, pt, member) \
    type var = 0; \
    for (unsigned i = 0; i < pt.weight(); i++) { \
        var += pt.get_value(i).member; \
    }

#define PER_THREAD_VECTOR_SUM(type, var, pt, index, member) \
    type var = 0; \
    for (unsigned i = 0; i < pt.weight(); i++) { \
        var += pt.get_value(i)[index].member; \
    }



/** @file <click/sync.hh>
 * @brief Classes for synchronizing among multiple CPUs, particularly in the
 * Linux kernel.
 */

/** @class Spinlock
 * @brief A recursive spinlock for SMP Click threads.
 *
 * The Spinlock class abstracts a recursive spinlock, or polling mutex, in SMP
 * Click.  This is a type of mutual-exclusion lock in which acquiring the lock
 * is a polling operation (basically a "while (lock.acquired()) do nothing;"
 * loop).  Spinlocks can be used to synchronize access to shared data among
 * multiple Click SMP threads.  Spinlocks should not be held for long periods
 * of time: use them for quick updates and such.
 *
 * Spinlock operations do nothing unless Click was compiled with SMP support
 * (with --enable-multithread).  Therefore, Spinlock should not be used to,
 * for example, synchronize handlers with main element threads.  See also
 * SpinlockIRQ.
 *
 * The main Spinlock operations are acquire(), which acquires the lock, and
 * release(), which releases the lock.  attempt() acquires the lock only if it
 * can be acquired instantaneously.
 *
 * It is OK for a thread to acquire a lock it has already acquired, but you
 * must release it as many times as you have acquired it.
 *
 * @sa SimpleSpinlock, SpinlockIRQ
 */
class Spinlock { public:

    inline Spinlock();
    inline ~Spinlock();

    inline void acquire();
    inline void release();
    inline bool attempt();
    inline bool nested() const;

#if CLICK_MULTITHREAD_SPINLOCK
  private:

    atomic_uint32_t _lock;
    int32_t _depth;
    click_processor_t _owner;
#endif

};

/** @brief Create a Spinlock. */
inline
Spinlock::Spinlock()
#if CLICK_MULTITHREAD_SPINLOCK
    : _depth(0), _owner(click_invalid_processor())
#endif
{
#if CLICK_MULTITHREAD_SPINLOCK
    _lock = 0;
#endif
}

inline
Spinlock::~Spinlock()
{
#if CLICK_MULTITHREAD_SPINLOCK
    if (_depth != 0)
	click_chatter(SPINLOCK_ASSERTLEVEL "Spinlock::~Spinlock(): assertion \"_depth == 0\" failed");
#endif
}

/** @brief Acquires the Spinlock.
 *
 * On return, this thread has acquired the lock.  The function will spin
 * indefinitely until the lock is acquired.  It is OK to acquire a lock you
 * have already acquired, but you must release it as many times as you have
 * acquired it.
 */
inline void
Spinlock::acquire()
{
#if CLICK_MULTITHREAD_SPINLOCK
    click_processor_t my_cpu = click_get_processor();
    if (_owner != my_cpu) {
	while (_lock.swap(1) != 0)
	    do {
		click_relax_fence();
	    } while (_lock != 0);
	_owner = my_cpu;
    }
    _depth++;
#endif
}

/** @brief Attempts to acquire the Spinlock.
 * @return True iff the Spinlock was acquired.
 *
 * This function will acquire the lock and return true only if the Spinlock
 * can be acquired right away, without retries.
 */
inline bool
Spinlock::attempt()
{
#if CLICK_MULTITHREAD_SPINLOCK
    click_processor_t my_cpu = click_get_processor();
    if (_owner != my_cpu) {
	if (_lock.swap(1) != 0) {
	    click_put_processor();
	    return false;
	}
	_owner = my_cpu;
    }
    _depth++;
    return true;
#else
    return true;
#endif
}

/** @brief Releases the Spinlock.
 *
 * The Spinlock must have been previously acquired by either Spinlock::acquire
 * or Spinlock::attempt.
 */
inline void
Spinlock::release()
{
#if CLICK_MULTITHREAD_SPINLOCK
    if (unlikely(_owner != click_current_processor()))
	click_chatter(SPINLOCK_ASSERTLEVEL "Spinlock::release(): assertion \"owner == click_current_processor()\" failed");
    if (likely(_depth > 0)) {
	if (--_depth == 0) {
	    _owner = click_invalid_processor();
	    _lock = 0;
	}
    } else
	click_chatter(SPINLOCK_ASSERTLEVEL "Spinlock::release(): assertion \"_depth > 0\" failed");
    click_put_processor();
#endif
}

/** @brief Returns true iff the Spinlock has been acquired more than once by
 * the current thread.
 */
inline bool
Spinlock::nested() const
{
#if CLICK_MULTITHREAD_SPINLOCK
    return _depth > 1;
#else
    return false;
#endif
}


/** @class SimpleSpinlock
 * @brief A non-recursive spinlock for SMP Click threads.
 *
 * The Spinlock class abstracts a non-recursive spinlock, or polling mutex, in
 * SMP Click.  This is a type of mutual-exclusion lock in which acquiring the
 * lock is a polling operation (basically a "while (lock.acquired()) do
 * nothing;" loop).  Spinlocks can be used to synchronize access to shared
 * data among multiple Click SMP threads.  Spinlocks should not be held for
 * long periods of time: use them for quick updates and such.
 *
 * Spinlock operations do nothing unless Click was compiled with SMP support
 * (with --enable-multithread).  Therefore, Spinlock should not be used to,
 * for example, synchronize handlers with main element threads.  See also
 * SpinlockIRQ.
 *
 * The main Spinlock operations are acquire(), which acquires the lock, and
 * release(), which releases the lock.  attempt() acquires the lock only if it
 * can be acquired instantaneously.
 *
 * It is NOT OK for a thread to acquire a lock it has already acquired.
 *
 * @sa Spinlock, SpinlockIRQ
 */
class SimpleSpinlock { public:

    inline SimpleSpinlock();
    inline ~SimpleSpinlock();

    inline void acquire();
    inline void release();
    inline bool attempt();

#if CLICK_LINUXMODULE
  private:
    spinlock_t _lock;
#elif CLICK_MULTITHREAD_SPINLOCK
  private:
    atomic_uint32_t _lock;
#endif

};

/** @brief Create a SimpleSpinlock. */
inline
SimpleSpinlock::SimpleSpinlock()
{
#if CLICK_LINUXMODULE
    spin_lock_init(&_lock);
#elif CLICK_MULTITHREAD_SPINLOCK
    _lock = 0;
#endif
}

inline
SimpleSpinlock::~SimpleSpinlock()
{
}

/** @brief Acquires the SimpleSpinlock.
 *
 * On return, this thread has acquired the lock.  The function will spin
 * indefinitely until the lock is acquired.
 */
inline void
SimpleSpinlock::acquire()
{
#if CLICK_LINUXMODULE
    spin_lock(&_lock);
#elif CLICK_MULTITHREAD_SPINLOCK
    while (_lock.swap(1) != 0)
	do {
	    click_relax_fence();
	} while (_lock != 0);
#endif
}

/** @brief Attempts to acquire the SimpleSpinlock.
 * @return True iff the SimpleSpinlock was acquired.
 *
 * This function will acquire the lock and return true only if the
 * SimpleSpinlock can be acquired right away, without retries.
 */
inline bool
SimpleSpinlock::attempt()
{
#if CLICK_LINUXMODULE
    return spin_trylock(&_lock);
#elif CLICK_MULTITHREAD_SPINLOCK
    return _lock.swap(1) == 0;
#else
    return true;
#endif
}

/** @brief Releases the SimpleSpinlock.
 *
 * The SimpleSpinlock must have been previously acquired by either
 * SimpleSpinlock::acquire or SimpleSpinlock::attempt.
 */
inline void
SimpleSpinlock::release()
{
#if CLICK_LINUXMODULE
    spin_unlock(&_lock);
#elif CLICK_MULTITHREAD_SPINLOCK
    _lock = 0;
#endif
}


/** @class SpinlockIRQ
 * @brief A spinlock that disables interrupts.
 *
 * The SpinlockIRQ class abstracts a spinlock, or polling mutex, that also
 * turns off interrupts.  Spinlocks are a type of mutual-exclusion lock in
 * which acquiring the lock is a polling operation (basically a "while
 * (lock.acquired()) do nothing;" loop).  The SpinlockIRQ variant can be used
 * to protect Click data structures from interrupts and from other threads.
 * Very few objects in Click need this protection; the Click Master object,
 * which protects the task list, uses it, but that's hidden from users.
 * Spinlocks should not be held for long periods of time: use them for quick
 * updates and such.
 *
 * In the Linux kernel, SpinlockIRQ is equivalent to a combination of
 * local_irq_save and the spinlock_t type.
 *
 * The SpinlockIRQ operations are acquire(), which acquires the lock, and
 * release(), which releases the lock.
 *
 * It is NOT OK for a SpinlockIRQ thread to acquire a lock it has already
 * acquired.
 */
class SpinlockIRQ { public:

    inline SpinlockIRQ();

#if CLICK_LINUXMODULE
    typedef unsigned long flags_t;
#else
    typedef int flags_t;
#endif

    inline flags_t acquire() CLICK_ALWAYS_INLINE;
    inline void release(flags_t) CLICK_ALWAYS_INLINE;

#if CLICK_LINUXMODULE
  private:
    spinlock_t _lock;
#elif CLICK_USERLEVEL && HAVE_MULTITHREAD
  private:
    Spinlock _lock;
#endif

};

/** @brief Creates a SpinlockIRQ. */
inline
SpinlockIRQ::SpinlockIRQ()
{
#if CLICK_LINUXMODULE
    spin_lock_init(&_lock);
#endif
}

/** @brief Acquires the SpinlockIRQ.
 * @return The current state of the interrupt flags.
 */
inline SpinlockIRQ::flags_t
SpinlockIRQ::acquire()
{
#if CLICK_LINUXMODULE
    flags_t flags;
    spin_lock_irqsave(&_lock, flags);
    return flags;
#elif CLICK_USERLEVEL && HAVE_MULTITHREAD
    _lock.acquire();
    return 0;
#else
    return 0;
#endif
}

/** @brief Releases the SpinlockIRQ.
 * @param flags The value returned by SpinlockIRQ::acquire().
 */
inline void
SpinlockIRQ::release(flags_t flags)
{
#if CLICK_LINUXMODULE
    spin_unlock_irqrestore(&_lock, flags);
#elif CLICK_USERLEVEL && HAVE_MULTITHREAD
    (void) flags;
    _lock.release();
#else
    (void) flags;
#endif
}


// read-write lock
//
// on read: acquire local read lock
// on write: acquire every read lock
//
// alternatively, we could use a read counter and a write lock. we don't do
// that because we'd like to avoid a cache miss for read acquires. this makes
// reads very fast, and writes more expensive

/** @class ReadWriteLock
 * @brief A read/write lock.
 *
 * The ReadWriteLock class abstracts a read/write lock in SMP Click.  Multiple
 * SMP Click threads can hold read locks simultaneously, but if any thread
 * holds a write lock, then no other thread holds any kind of lock.  The
 * read/write lock is implemented with Spinlock objects, so acquiring a lock
 * is a polling operation.  ReadWriteLocks can be used to synchronize access
 * to shared data among multiple Click SMP threads.  ReadWriteLocks should not
 * be held for long periods of time.
 *
 * ReadWriteLock operations do nothing unless Click was compiled with
 * --enable-multithread.  Therefore, ReadWriteLock should not be used to, for
 * example, synchronize handlers with main element threads.
 *
 * The main ReadWriteLock operations are acquire_read() and acquire_write(),
 * which acquire the lock for reading or writing, respectively, and
 * release_read() and release_write(), which similarly release the lock.
 * attempt_read() and attempt_write() acquire the lock only if it can be
 * acquired instantaneously.
 *
 * It is OK for a thread to acquire a lock it has already acquired, but you
 * must release it as many times as you have acquired it.
 *
 * ReadWriteLock objects are relatively large in terms of memory usage; don't
 * create too many of them.
 */
class ReadWriteLock { public:

    inline ReadWriteLock();
    inline ~ReadWriteLock();

    inline void acquire_read();
    inline bool attempt_read();
    inline void release_read();
    inline void acquire_write();
    inline bool attempt_write();
    inline void release_write();

  private:
    // allocate a cache line for every member
    per_thread<Spinlock> _l;

};

/** @brief Creates a ReadWriteLock. */
inline
ReadWriteLock::ReadWriteLock()
{
}

inline
ReadWriteLock::~ReadWriteLock()
{
}

/** @brief Acquires the ReadWriteLock for reading.
 *
 * On return, this thread has acquired the lock for reading.  The function
 * will spin indefinitely until the lock is acquired.  It is OK to acquire a
 * lock you have already acquired, but you must release it as many times as
 * you have acquired it.
 *
 * @sa Spinlock::acquire
 */
inline void
ReadWriteLock::acquire_read()
{
#if HAVE_MULTITHREAD
    _l->acquire();
#endif
}

/** @brief Attempts to acquire the ReadWriteLock for reading.
 * @return True iff the ReadWriteLock was acquired.
 *
 * This function will acquire the lock for reading and return true only if the
 * ReadWriteLock can be acquired right away, without retries.
 */
inline bool
ReadWriteLock::attempt_read()
{
#if HAVE_MULTITHREAD
    bool result = _l->attempt();
    if (!result)
	click_put_processor();
    return result;
#else
    return true;
#endif
}

/** @brief Releases the ReadWriteLock for reading.
 *
 * The ReadWriteLock must have been previously acquired by either
 * ReadWriteLock::acquire_read or ReadWriteLock::attempt_read.  Do not call
 * release_read() on a lock that was acquired for writing.
 */
inline void
ReadWriteLock::release_read()
{
#if HAVE_MULTITHREAD
    _l->release();
    click_put_processor();
#endif
}

/** @brief Acquires the ReadWriteLock for writing.
 *
 * On return, this thread has acquired the lock for writing.  The function
 * will spin indefinitely until the lock is acquired.  It is OK to acquire a
 * lock you have already acquired, but you must release it as many times as
 * you have acquired it.
 *
 * @sa ReadWriteLock::acquire_read
 */
inline void
ReadWriteLock::acquire_write()
{
#if HAVE_MULTITHREAD
    for (unsigned i = 0; i < _l.weight(); i++)
        _l.get_value(i).acquire();
#endif
}

/** @brief Attempts to acquire the ReadWriteLock for writing.
 * @return True iff the ReadWriteLock was acquired.
 *
 * This function will acquire the lock for writing and return true only if the
 * ReadWriteLock can be acquired right away, without retries.  Note, however,
 * that acquiring a ReadWriteLock requires as many operations as there are
 * CPUs.
 *
 * @sa ReadWriteLock::attempt_read
 */
inline bool
ReadWriteLock::attempt_write()
{
#if HAVE_MULTITHREAD
    bool all = true;
    unsigned i;
    for (unsigned i = 0; i < _l.weight(); i++) {
        if (!(_l.get_value(i).attempt())) {
            all = false;
            break;
        }
    }
    if (!all) {
        for (unsigned j = 0; j < i; j++)
            _l.get_value(j).release();
    }
    return all;
#else
    return true;
#endif
}

/** @brief Releases the ReadWriteLock for writing.
 *
 * The ReadWriteLock must have been previously acquired by either
 * ReadWriteLock::acquire_write or ReadWriteLock::attempt_write.  Do not call
 * release_write() on a lock that was acquired for reading.
 *
 * @sa ReadWriteLock::release_read
 */
inline void
ReadWriteLock::release_write()
{
#if HAVE_MULTITHREAD
    for (unsigned i = 0; i < _l.weight(); i++)
        _l.get_value(i).release();
#endif
}

/**
 * Fake lock
 *
 * Usefull for templating differently protected structure
 */
class nolock { public:
    nolock() {}

    inline void read_begin() {}

    inline void read_end() {}

    inline void read_get() {}

    inline void write_begin() {}

    inline void write_end() {}

    inline void set_max_writers(int) {}
};

CLICK_ENDDECLS
#undef SPINLOCK_ASSERTLEVEL
#endif
