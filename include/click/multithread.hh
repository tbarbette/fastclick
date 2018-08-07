// -*- c-basic-offset: 4 -*-
#ifndef CLICK_MULTITHREAD_HH
#define CLICK_MULTITHREAD_HH

#include <click/config.h>
#include <click/glue.hh>
#include <click/vector.hh>
#include <click/bitvector.hh>
#include <click/machine.hh>
#include <click/sync.hh>

#if CLICK_LINUXMODULE
# error This file is not meant for Kernel mode
#endif

CLICK_DECLS

/*
 * Read efficient version of per thread, by giving a bitvector telling
 * which threads will be used, a mapping can be used to only read from
 * variables actually used
 *
 * Mapping is from index inside the storage vector to thread id
 */
template <typename T>
class per_thread_oread : public per_thread<T> { public:
    void compress(Bitvector usable) {
        mapping.resize(usable.weight(),0);
        int id = 0;
        for (int i = 0; i < usable.size(); i++) {
            if (usable[i]) {
                mapping[id] = i;
                id++;
            }
        }
        per_thread<T>::_size = id;
    }

    inline T& get_value(int i) const{
        return per_thread<T>::storage[mapping[i]].v;
    }

    inline void set_value(int i, T v) {
        per_thread<T>::storage[mapping[i]].v = v;
    }

private:
    Vector<unsigned int> mapping;
};

/*
 * Memory efficient version which only duplicate the variable per thread
 * actually used, given a bitvector telling which one they are. However
 * this comes at the price of one more indirection as we
 * need a table to map thread ids to positions inside the storage
 * vector. The mapping table itself is a vector of integers, so
 * if your data structure is not bigger than two ints, it is not worth it.
 */
template <typename T>
class per_thread_omem { private:
    typedef struct {
        T v;
    } CLICK_CACHE_ALIGN AT;

    AT* storage;
    Vector<unsigned int> mapping;
    unsigned _size;

public:
    per_thread_omem() : storage(0),mapping(),_size(0) {

    }

    ~per_thread_omem() {
        if (storage)
            delete[] storage;
    }

    void initialize(Bitvector usable, T v=T()) {
        _size = usable.weight();
        storage = new AT[_size];
        mapping.resize(click_max_cpu_ids(), 0);
        int id = 0;
        for (unsigned i = 0; i < click_max_cpu_ids(); i++) {
            if (usable[i]) {
                storage[id].v = v;
                mapping[i] = id++;
            }
        }
    }

    inline T* operator->() const {
        return &(storage[mapping[click_current_cpu_id()]].v);
    }
    inline T& operator*() const {
        return storage[mapping[click_current_cpu_id()]].v;
    }

    inline T& operator+=(const T& add) const {
        storage[mapping[click_current_cpu_id()]].v += add;
        return storage[mapping[click_current_cpu_id()]].v;
    }

    inline T& operator++() const { // prefix ++
        return ++(storage[mapping[click_current_cpu_id()]].v);
    }

    inline T operator++(int) const { // postfix ++
        return storage[mapping[click_current_cpu_id()]].v++;
    }

    inline T& operator--() const {
        return --(storage[mapping[click_current_cpu_id()]].v);
    }

    inline T operator--(int) const {
        return storage[mapping[click_current_cpu_id()]].v--;
    }

    inline T& operator=(T value) const {
        set(value);
        return storage[mapping[click_current_cpu_id()]].v;
    }

    inline void set(T v) {
        storage[mapping[click_current_cpu_id()]].v = v;
    }

    inline void setAll(T v) {
        for (int i = 0; i < weight(); i++)
            storage[i].v = v;
    }

    inline T& get() const{
        return storage[mapping[click_current_cpu_id()]].v;
    }

    inline T& get_value_for_thread(int thread_id) const{
        return storage[mapping[thread_id]].v;
    }

    inline void set_value_for_thread(int thread_id, T v) {
        storage[mapping[thread_id]].v = v;
    }

    inline T& get_value(int i) const {
        return storage[i].v;
    }

    inline void set_value(int i, T v) {
        storage[i] = v;
    }

    inline unsigned weight() const {
        return _size;
    }
};

/**
 * Convenient class to have MP and non-MP version of the same thimg eg :
 *
 * per_thread_arithmetic::sum(a); will work if a is an int or if a is a per_thread<int>
 */
class per_thread_arithmetic { public:

    static int sum(int i) {
        return i;
    }

    static unsigned usum(unsigned i) {
        return i;
    }

    static int sum(per_thread<int> &v) {
        PER_THREAD_SUM(unsigned, s, v);
        return s;
    }

    static unsigned sum(per_thread<unsigned> &v) {
        PER_THREAD_SUM(unsigned, s, v);
        return s;
    }

    static void set(int &i, int v) {
        i = v;
    }

    static void uset(unsigned &i, unsigned v) {
        i = v;
    }

    static void set(per_thread<int> &pt, int v) {
        PER_THREAD_SET(pt , v);
    }

    static void uset(per_thread<unsigned> &pt, unsigned v) {
        PER_THREAD_SET(pt , v);
    }

#if HAVE_INT64_TYPES
    static int64_t sum_long(int64_t i) {
        return i;
    }

    static uint64_t usum_long(uint64_t i) {
        return i;
    }

    static void set_long(int64_t &i, int64_t v) {
        i = v;
    }

    static void uset_long(uint64_t &i, uint64_t v) {
        i = v;
    }

    static int64_t sum_long(per_thread<int64_t> &v) {
        PER_THREAD_SUM(int64_t, s, v);
        return s;
    }

    static int64_t usum_long(per_thread<uint64_t> &v) {
        PER_THREAD_SUM(uint64_t, s, v);
        return s;
    }

    static void set_long(per_thread<int64_t> &pt, int64_t v) {
        PER_THREAD_SET(pt, v);
    }

    static void uset_long(per_thread<uint64_t> &pt, uint64_t v) {
        PER_THREAD_SET(pt, v);
    }
#endif
};

/**
 * unprotected_rcu_singlewriter implements a very simple SINGLE
 * writer, multiple-reader rcu.
 *
 * rcu allows to do a kind of atomic write with any structure. A temp copy
 * is made for the writer, and when the writer has finished his write, he
 * commits the change by changing the pointer for all subsequent readers.
 *
 * Unlike usual RCU implementation, this one does not need lock for readers,
 * however memory will be corrupted if N subsequent writes happens while a
 * reader is obtaining a reference. If N cannot be asserted in your use-case,
 * this structure is not for you. Hence, the unprotected in the name to attract
 * attention to this fact.
 *
 * This works by having a ring of N elements. The writer updates the next
 * element of the ring, and when it has finished, it updates the current ring
 * index to the next element. For fast wrap-up computing, N must be
 * a power of 2 and at least 2.
 *
 * Using a ring allows for getting rid of atomic reference count, but introduces
 * the N problem.
 *
 * For this to work, the template class must be able to copy itself with "=", eg :
 * T b;
 * b.do_something();
 * T a = b;
 * This is true for primitive, but you may need to implement operator= for class
 *
 * Usage :
 *   unprotected_rcu_singlewriter<my_struct,2> rcu_struct;
 *  reader :
 *   click_chatter("Member is %d.", rcu_struct.read().member);
 *   click_chatter("Member is %d.", rcu_struct->member); //Same, more convenient
 *  writer :
 *   my_struct &tmp_write = rcu_struct->write_begin();
 *   tmp_write->member++;
 *   rcu_struct->write_commit();
 */
template <typename T, int N>
class unprotected_rcu_singlewriter { public:

    unprotected_rcu_singlewriter() : rcu_current(0) {
    }

    unprotected_rcu_singlewriter(T v) : rcu_current(0) {
        storage[rcu_current] = v;
    }

    ~unprotected_rcu_singlewriter() {
    }

    inline void initialize(T v) {
        storage[rcu_current] = v;
    }

    inline const T& read_begin() {
        return storage[rcu_current];
    }

    inline void read_end() {

    }

    inline const T& read() const {
        return storage[rcu_current];
    }

    inline const T operator*() const {
        return read();
    }

    inline const T* operator->() const {
        return &read();
    }

    inline T& write_begin() {
        int rcu_next = (rcu_current + 1) & (N - 1);
        storage[rcu_next] = storage[rcu_current];
        return storage[rcu_next];
    }

    inline void write_commit() {
        click_write_fence();
        rcu_current = (rcu_current + 1) & (N - 1);
    }

    inline void write_abort() {
    }

    inline const T& read_begin(int) {
        return read_begin();
    }

    inline void read_end(int) {
        return read_end();
    }

protected:
    T storage[N];
    volatile int rcu_current;
};


/**
 * unprotected_rcu works like unprotected_rcu_singlewriter but can have multiple writer.
 *
 * Usage is the same but attempt to write use a spinlock. In handlers or
 * other slow path, use this version. In other cases, double check if there could
 * be multiple writers at the same time, and if not, use unprotected_rcu_singlewriter.
 *
 * Failing to call write_commit() will cause a dead lock !!!
 *
 * To abort a write, use write_abort();
 */
template <typename T,int N>
class unprotected_rcu : public unprotected_rcu_singlewriter<T,N> { public:

    unprotected_rcu() : _write_lock() {

    }

    inline T& write_begin() {
        _write_lock.acquire();
        return this->unprotected_rcu_singlewriter<T,N>::write_begin();
    }

    inline void write_commit() {
        this->unprotected_rcu_singlewriter<T,N>::write_commit();
        _write_lock.release();
    }

    inline void write_abort() {
        _write_lock.release();
    }

private:
    Spinlock _write_lock;
};

/**
 * Wrap a pointer to V with a reference count. Call get() and put()
 *  to get and put reference. When created, it has a reference count of 1
 *  when the call to put() goes to 0, it calls delete on the pointer.
 */
template <typename T>
class shared { public:

    shared() {
        _refcnt = 0;
    }

    shared(T p) : _p(p) {
        _refcnt = 0;
    }

    class ptr { public:
        inline ptr() : _v(0) {};
        inline ptr(shared<T>* v) : _v(v) {_v->get();}
        inline ~ptr() {
            release();
        }

        inline T* operator->() {
             return &_v->_p;
        }

        inline T* get() {
            return &_v->_p;
        }

        inline T& operator*() {
            return _v->_p;
        }

        inline ptr(const ptr &o) {
            _v = o._v;
            o._v = 0;
        }

        inline uint32_t refcnt() {
            return _v->_refcnt;
        }

        inline void assign(shared<T>* v) {
            release();
            _v = v;
            _v->get();
        }

        inline void assign_release(ptr &o) {
            release();
            _v = o._v;
            o._v = 0;
        }

        inline void operator=(const ptr &o) {
            release();
            _v = o._v;
            if (_v)
                _v->get();
        }

        inline void release() {
            if (_v)
                _v->put();
            _v = 0;
        }

        inline operator bool() const {
            return _v;
        }

    private:
        mutable shared<T>* _v;
    };

    //shared does not support write_ptr, we typedef for convenience
    typedef ptr write_ptr;

    inline ptr operator->() {
        return ptr(this);
    }

    inline ptr read() {
        return ptr(this);
    }

    inline ptr write() {
        return write_ptr(this);
    }


    inline void get(ptr &o) {
        o.assign(this);
    }

    uint32_t refcnt() {
        return _refcnt;
    }

    T* unprotected_ptr() {
        return &_p;
    }

private:
    T _p;
    atomic_uint32_t _refcnt;

    inline void get() {
        _refcnt++;
    }

    inline void put() {
        click_read_fence();
        _refcnt--;
    }
};

/**
 * Inherit this class to allow multiple reader or a single write access
 * to the inherited class by enclosing data access by read_begin()/read_end()
 * and write_begin()/write_end()
 *
 * Carefull : calling write_begin while read is held or vice versa will end up in deadlock
 */

class RWLock { public:
    RWLock() {
        _refcnt = 0;
    }

    inline void read_begin() {
        uint32_t current_refcnt;
        do {
            current_refcnt = _refcnt;
        } while ((int32_t)current_refcnt < 0 || (_refcnt.compare_swap(current_refcnt,current_refcnt+1) != current_refcnt));
    }

    inline void read_end() {
        click_read_fence();
        _refcnt--;
    }

    inline void read_get() {
        _refcnt++;
    }

    inline void write_begin() {
        while (_refcnt.compare_swap(0,-1) != 0) click_relax_fence();
    }

    inline bool write_attempt() {
        return (_refcnt.compare_swap(0,-1) == 0);
    }

    /**
     * Grab a second write reference on an already held write
     * @pre refcnt < 0
     * @pre write_begin() has been called before
     */
    inline void write_get() {
        _refcnt--;
    }

    inline void write_end() {
        click_write_fence();
        _refcnt++;
    }

    inline void write_to_read() {
        click_write_fence();
        assert(_refcnt == (uint32_t)-1);
        _refcnt = 1;
    }

    /**
     * Try to become a writer while read is held. This is unlikely but
     * this may fail, in which case this function returns false. You
     * have to assume that any pointer to the protected object is now
     * bad (a writer has grabbed the write lock while you released the read)
     * The unlikeliness of this event makes this function worth it,
     * if this is unacceptable, directly grab a write reference.
     * TLDR : if false, you have loosed your read lock and neither
     * acquired the write
     */
    inline bool read_to_write() CLICK_WARN_UNUSED_RESULT;

    inline uint32_t refcnt() {
        return _refcnt;
    }

private:
    atomic_uint32_t _refcnt;

};


template <class V>
class __rwlock : public RWLock { public:
    __rwlock() : _v(){
    }

    __rwlock(V v) : _v(v) {
    }

    V _v;

    V* operator->() {
        return &_v;
    }

    V& operator*() {
        return _v;
    }
};

/**
 * Read XOR Write lock. Allow either multiple reader or multiple
 * writer. When a reader arrives, writers stop taking the usecount. The reader
 * has access once all writer finish.
 *
 * To stop writer from locking, the reader will CAS a very low value.
 *
 * If max_writer is 1, this becomes rwlock, but with a priority on the reads
 */

class rXwlockPR { public:
    rXwlockPR() : max_write(-65535) {
        _refcnt = 0;
    }

    rXwlockPR(int32_t max_writers) {
        _refcnt = 0;
        set_max_writers(max_writers);
    }

    void set_max_writers(int32_t max_writers) {
        assert(max_writers < 65535);
        read_begin();
        max_write = - max_writers;
        read_end();
    }

    inline void read_begin() {
        uint32_t current_refcnt;
        do {
            current_refcnt = _refcnt;
            if (unlikely((int32_t)current_refcnt < 0)) {
                if ((int32_t)current_refcnt <= -65536) {
                    //Just wait for the other reader out there to win
                } else {
                    if (_refcnt.compare_swap(current_refcnt,current_refcnt - 65536) == current_refcnt) {
                        //We could lower the value, so wait for it to reach -65536 (0 writer but one reader waiting) and continue
                        do {
                            click_relax_fence();
                        } while((int32_t)_refcnt != -65536);
                        //When it is -65536, driver cannot take it and reader are waiting, so we can set it directly
                        _refcnt = 1;
                        break;
                    }
                }
            } else { // >= 0, just grab another reader (>0)
                if (likely(_refcnt.compare_swap(current_refcnt,current_refcnt+1) == current_refcnt))
                    break;
            }
            click_relax_fence();
        } while (1);
    }

    inline void read_end() {
        click_read_fence();
        _refcnt--;
    }

    inline void read_get() {
        _refcnt++;
    }

    inline void write_begin() {
        uint32_t current_refcnt;
        do {
            current_refcnt = _refcnt;
            if (likely((int32_t)current_refcnt <= 0 && (int32_t)current_refcnt > max_write)) {
                if (_refcnt.compare_swap(current_refcnt,current_refcnt - 1) == current_refcnt)
                    break;
            }
            click_relax_fence();
        } while (1);
    }

    inline void write_end() {
        click_write_fence();
        _refcnt++;
    }

private:
    atomic_uint32_t _refcnt;
    int32_t max_write;
} CLICK_CACHE_ALIGN;

/**
 * Read XOR Write lock. Allow either multiple reader or multiple
 * writer. When a reader arrives, writers stop taking the usecount. The reader
 * has access once all writer finish.
 *
 * To stop writer from locking, the reader will CAS a very low value.
 *
 * If max_writer is 1, this becomes rwlock, but with a priority on the reads
 */

class rXwlockPW { public:
    rXwlockPW() : max_write(-65535) {
        _refcnt = 0;
    }

    rXwlockPW(int32_t max_writers) {
        _refcnt = 0;
        set_max_writers(max_writers);
    }

    void set_max_writers(int32_t max_writers) {
        assert(max_writers < 65535);
        write_begin();
        max_write = - max_writers;
        write_end();
    }

    inline void write_begin() {
        uint32_t current_refcnt;
        do {
            current_refcnt = _refcnt;
            if (unlikely((int32_t)current_refcnt < 0)) {
                if ((int32_t)current_refcnt <= -65536) {
                    //Just wait for the other reader out there to win
                } else {
                    if (_refcnt.compare_swap(current_refcnt,current_refcnt - 65536) == current_refcnt) {
                        //We could lower the value, so wait for it to reach -65536 (0 writer but one reader waiting) and continue
                        do {
                            click_relax_fence();
                        } while((int32_t)_refcnt != -65536);
                        //When it is -65536, driver cannot take it and reader are waiting, so we can set it directly
                        _refcnt = 1;
                        break;
                    }
                }
            } else { // >= 0, just grab another reader (>0)
                if (likely(_refcnt.compare_swap(current_refcnt,current_refcnt+1) == current_refcnt))
                    break;
            }
            click_relax_fence();
        } while (1);
    }

    inline void write_end() {
        click_write_fence();
        _refcnt--;
    }

    inline void write_get() {
        _refcnt++;
    }

    inline void read_begin() {
        uint32_t current_refcnt;
        do {
            current_refcnt = _refcnt;
            if (likely((int32_t)current_refcnt <= 0 && (int32_t)current_refcnt > max_write)) {
                if (_refcnt.compare_swap(current_refcnt,current_refcnt - 1) == current_refcnt)
                    break;
            }
            click_relax_fence();
        } while (1);
    }

    inline void read_end() {
        click_read_fence();
        _refcnt++;
    }


private:
    atomic_uint32_t _refcnt;
    int32_t max_write;
} CLICK_CACHE_ALIGN;



class rXwlock { public:
    rXwlock() {
        _refcnt = 0;
    }

    inline void read_begin() {
        uint32_t current_refcnt;
        current_refcnt = _refcnt;
        while ((int32_t)current_refcnt < 0 || _refcnt.compare_swap(current_refcnt,current_refcnt+1) != current_refcnt) {
            click_relax_fence();
            current_refcnt = _refcnt;
        }
    }

    void set_max_writers(int32_t max_writers) {
        (void)max_writers;
    }



    inline void read_end() {
        click_read_fence();
        _refcnt--;
    }

    inline void read_get() {
        _refcnt++;
    }

    inline void write_begin() {
        uint32_t current_refcnt;
        current_refcnt = _refcnt;
        while ((int32_t)current_refcnt > 0 || _refcnt.compare_swap(current_refcnt,current_refcnt-1) != current_refcnt) {
            click_relax_fence();
            current_refcnt = _refcnt;
        }
    }

    inline void write_end() {
        click_write_fence();
        _refcnt++;
    }

private:
    atomic_uint32_t _refcnt;
} CLICK_CACHE_ALIGN;


/**
 * Shared-pointer based rwlock
 */
template <typename V>
class rwlock { public:

    rwlock() : _v() {
    }

    rwlock(V v) : _v(v) {
    }

    class ptr { public :
        ptr(const ptr &o) {
            _p = o._p;
            if (_p)
                _p->read_get();
        }

        void release() {
            if (_p)
                _p->read_end();
            _p = 0;
        }

        ~ptr() {
            release();
        }

        const V* operator->() const {
            return &_p->_v;
        }

        const V& operator*() const {
            return _p->_v;
        }

        void operator=(const ptr &o) {
            _p = o._p;
            if (_p) {
                _p->read_begin();
            }
        }

        inline uint32_t refcnt() {
            return _p->refcnt();
        }

        /**
         * Assign a rwlock to this pointer, grabbing the read lock.
         */
        inline void assign(rwlock<V>* v) {
            release();
            _p = &v->_v;
            if (_p)
                _p->read_begin();
        }

        inline operator bool() const {
            return _p;
        }

        ptr() : _p(0) {};
    private:
        __rwlock<V>* _p;

        ptr(__rwlock<V>* p) : _p(p) {}; //Read must be held

        friend class rwlock<V>;
    };

    class write_ptr { public :
        write_ptr(const write_ptr &o) {
        _p = o._p;
            if (_p) {
                _p->write_get();
            }
        }

        void release() {
            if (_p)
                _p->write_end();
            _p = 0;
        }

        ~write_ptr() {
            release();
        }

        V* operator->() {
            return &_p->_v;
        }

        V& operator*() {
            return _p->_v;
        }

        void operator=(const write_ptr &o) {
            if (o._p)
                _p->write_end();
            _p = o._p;
            if (_p) {
                _p->write_get();
            }
        }

        uint32_t refcnt() {
            return _p->refcnt();
        }

        /**
         * Assign a rwlock to this pointer, grabbing the write lock.
         */
        inline void assign(rwlock<V>* v) {
            release();
            _p = &v->_v;
            if (_p)
                _p->write_begin();
        }

        /**
         * Copy the ownership of a given write pointer and release that
         * pointer.
         */
        inline void assign_release(write_ptr& o) {
            release();
            _p = o._p;
            o._p = 0;
        }

        inline operator bool() const {
            return _p;
        }

        write_ptr() : _p(0) {};
    private:
        __rwlock<V>* _p;

        write_ptr(__rwlock<V>* p) : _p(p) {}; //Write must be held

        friend class rwlock<V>;
    };

    ptr read() {
        _v.read_begin();
        return ptr(&_v);
    }

    write_ptr write() {
        _v.write_begin();
        return write_ptr(&_v);
    }

    /**
     * Destroys the given read pointer and return a write pointer.
     *
     * This may fail, in which case the returned and the given pointer
     * are both null.
     *
     * In any case attempting to access the given read pointer will cause segfault
     */
    write_ptr read_to_write(const ptr &o) {
        o._p = 0;
        if (_v.read_to_write()) {
            return write_ptr(_v);
        }
        return write_ptr();
    }

    /**
     * Destroys the given write pointer and return a read pointer.
     *
     * Attempting to access the given write pointer afterwards will cause segfault
     */
    ptr write_to_read(const write_ptr &o) {
        o._p = 0;
        _v.write_to_read();
        return const_ptr(_v);
    }

    V* unprotected_ptr() {
        return &_v._v;
    }

    uint32_t refcnt() {
        return _v.refcnt();
    }
private:
    __rwlock<V> _v;
};

inline bool
RWLock::read_to_write() {
       /* Sadly, read_to_write is more complex than write_to_read,
        * because
        * two readers could want to become writer at the same time,
        * probably having some reference that will become invalid if
        * someone else grab the write. In this case one of them must
        * fail and retry again, or we'll have a deadlock.
        *
        * The trick is to add 1000 to the refcnt, only one potential
        * writer will be able to do that so after that we can wait for
        * the count to be 1000000 (no more reader) and make it -1
        */
       click_write_fence();

       //All that being said, we make a first attempt in case we would be the only reader
       if (_refcnt.compare_swap(1,-1) == 1)
               return true;

       uint32_t current_refcnt;
       uint32_t new_refcnt;
       do {
           current_refcnt = _refcnt;
           new_refcnt = current_refcnt+1000000;
           if (current_refcnt > 1000000) {
               read_end();
               click_relax_fence();
               return false;
           }
       } while (_refcnt.compare_swap(current_refcnt,new_refcnt) != current_refcnt);
       while (_refcnt.compare_swap(1000001,-1) != 1000001) click_relax_fence();
       return true;
}

/**
 * click_rcu is the ultimate version of the RCU series, the more
 * protected one which will always work but also the slowest one.
 *
 * The problem of unprotected_rcu* is solved by keeping
 * a reference count of the amount of readers using any current
 * bucket. The writer will wait for all readers to finish before
 * overwriting the next available slot.
 *
 * Here, the writers exclude each other by setting the refcnt
 * to -1 atomically, if the value was 0. They will spinloop around
 * the refcnt until each other writer has finished.
 *
 * It is not as bad as a critical section/interrupt like in kernel,
 * but those lock operation still have a non-negligeable cost.
 *
 * It may be difficult to see that writer cannot overwrite a bucket that is
 * currently being accessed, here is a helping example :
 *   A reads index
 *   B write and advance
 *   C write and advance, check refcnt[A], CAS  0 -> -1
 *   A writes refcnt[A] -> CAS fail
 *
 * The ring has 2 values, so one writer can update one bucket while readers
 * may still read the other bucket.
 */
template <typename T>
class click_rcu { public:
    click_rcu() : rcu_current(0) {
        refcnt[0] = 0;
        refcnt[1] = 0;
    }

    click_rcu(T v) : rcu_current(0) {
        refcnt[0] = 0;
        refcnt[1] = 0;
        initialize(v);
    }

    ~click_rcu() {}

    inline void initialize(T v) {
        storage[rcu_current] = v;
    }

    /**
     * A loop is needed to avoid a race condition where thread A reads
     * rcu_current, then writer B does a full write, start a second write
     * before A update the refcnt. A would then inc the refcnt,
     * and get a reference while B is in fact writing that bucket.
     *
     * The solution is the following one:
     * When the refcnt for the next bucket is 0, the writer is writing -1 as
     * the refcnt. This is done atomicly using a CAS instruction.
     *
     * In the reader, we check if the refcnt is -1 (current write), and if not we CAS
     * refcnt+1. We loop until the CAS worked so we know that we made a "refcnt++"
     * without any writer starting to write in the bucket.
     *
     * Also, the same rcu_current_local must be passed to read_end
     * as we need to down the actual refcnt we incremented, and rcu_current
     * may be incremented by 1 before we finish our reading.
     */
    inline const T& read_begin(int &rcu_current_local) {
        uint32_t current_refcnt;
        do {
            rcu_current_local = rcu_current;
            click_read_fence();
            current_refcnt = refcnt[rcu_current_local].value();
            if (current_refcnt == (uint32_t)-1 || (refcnt[rcu_current_local].compare_swap(current_refcnt,current_refcnt+1) != current_refcnt)) {
                click_relax_fence();
            } else {
                break;
            }
        } while (1);
        click_write_fence();
        //The reference is holded now, we are sure that no writer will edit this until read_end is called.
        return storage[rcu_current_local];
    }

    inline void read_end(const int &rcu_current_local) {
        click_compiler_fence(); //No reorder of refcnt--
        refcnt[rcu_current_local]--;
    }

    inline T read() {
        int flags;
        T v = read_begin(flags);
        read_end(flags);
        return v;
    }

    /**
     * No need for lock of writers anymore. We atomicly set the refcnt to -1 if
     * the value is 0, if another writer is writing, it would have done the
     * same.
     * If no readers are reading, the refcnt would not be 0.
     */
    inline T& write_begin() {
        int rcu_next;
        retry:
        do {
            rcu_next = (rcu_current + 1) & 1;
        } while (refcnt[rcu_next].compare_swap(0,-1) != 0);

        /*As the other writer writes the refcnt before rcu_current, we could
         *have grabbed the new actual bucket*/
        if (rcu_next == rcu_current) {
            refcnt[rcu_next] = 0;
            goto retry;
        }

        //Refcnt is now -1, and we are the only writer
        storage[rcu_next] = storage[rcu_current];
        return storage[rcu_next];
    }

    inline void write_commit() {
        int rcu_next = (rcu_current + 1) & 1;
        rcu_current = rcu_next;
        click_write_fence();
        refcnt[rcu_next] = 0;
    }

    inline void write_abort() {
        /*Just set the refcnt to 0, as rcu_current is not changed, another writer
        will grab the same bucket, and the readers will not see a thing*/
        click_write_fence();
        refcnt[(rcu_current + 1) & 1] = 0;
    }

    inline void write(const T &v) {
        write_begin() = v;
        write_commit();
    }

protected:
    atomic_uint32_t refcnt[2];
    T storage[2];
    volatile int rcu_current;
};


/**
 * Fast RCU that avoids heavy locked CAS by using a per-thread writer epoch
 */
template <typename T>
class fast_rcu { public:
#define N 2
    fast_rcu() : _rcu_current(0), _epochs(0), _write_epoch(1) {
    }

    fast_rcu(T v) : _rcu_current(0), _epochs(0), _write_epoch(1){
        initialize(v);
    }

    ~fast_rcu() {}

    inline void initialize(T v) {
        _storage[_rcu_current].v = v;
    }

    inline const T& read_begin(int &) {
        int w_epoch = _write_epoch;
        *_epochs = w_epoch;
        click_write_fence(); //Actually read rcu_current after storing epoch
        int rcu_current_local = _rcu_current;
        click_read_fence();//Do not read _rcu_current later
        //The reference is holded now, we are sure that no writer will edit this until read_end is called.
        return _storage[rcu_current_local].v;
    }

    inline void read_end(const int &) {
        click_compiler_fence(); //No load or store after writing the epoch bak to 0
        *_epochs = 0;
    }

    inline T read() {
        int flags;
        T v = read_begin(flags);
        read_end(flags);
        return v;
    }

    inline T& write_begin(int& rcu_current_local) {

        _write_lock.acquire();
        rcu_current_local = _rcu_current;

        int rcu_next = (rcu_current_local + 1) & 1;
        int bad_epoch = (_write_epoch - N) + 1;

        unsigned i = 0;
        loop:
        for (; i < _epochs.weight(); i ++) {
            int te = _epochs.get_value(i);
            if (unlikely(te != 0 && te == bad_epoch)) {
                click_relax_fence();
                goto loop;
            }
            //TODO : if allow N > 2, compute a min_epoch at the same time so next writer can avoid looping
        }

        //All epochs are 0 (no accessed, or at least not to the current ptr)
        _storage[rcu_next].v = _storage[rcu_current_local].v;

        return _storage[rcu_next].v;
    }

    inline void write_commit(int rcu_current_local) {
        click_write_fence(); //Prevent write to finish after this
        _rcu_current = (rcu_current_local + 1) & 1;
        click_compiler_fence(); //Write epoch must happen after rcu_current, to be sure that thr old bucket is not read and accessed with the new epoch.
        ++_write_epoch;
        _write_lock.release();
    }

protected:

    typedef struct {
        T v;
    } CLICK_CACHE_ALIGN AT;

    volatile int _rcu_current;
    per_thread<volatile int> _epochs;
    AT _storage[2];
    volatile int _write_epoch;
    Spinlock _write_lock;

};

CLICK_ENDDECLS
#endif
