// -*- c-basic-offset: 4 -*-
#ifndef CLICK_MULTITHREAD_HH
#define CLICK_MULTITHREAD_HH

#include <click/config.h>
#include <click/glue.hh>
#include <click/vector.hh>
#include <click/bitvector.hh>
#include <click/machine.hh>
#include <click/sync.hh>

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
    typedef struct {
        T v;
    } CLICK_CACHE_ALIGN AT;

    void initialize(unsigned int n, T v) {
        _size = n;
        storage = new AT[_size];
        for (unsigned i = 0; i < n; i++) {
            storage[i].v = v;
        }
    }

    //Disable copy constructor. It will always be a user error
    per_thread (const per_thread<T> &);
public:
    explicit per_thread() {
        _size = click_max_cpu_ids();
        storage = new AT[_size];
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
        delete[] storage;
        initialize(max_cpu_id,v);
    }

    ~per_thread() {
        delete[] storage;
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
    inline unsigned weight() {
        return _size;
    }

    inline unsigned int get_mapping(int i) {
        return i;
    }
protected:
    AT* storage;
    unsigned _size;
};

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

    inline unsigned weight() {
        return _size;
    }
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
 * a power of 2.
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

CLICK_ENDDECLS
#endif
