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
    per_thread (const per_thread<T> &o);
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
     * Resize must be called if per_thread was initialized before click_max_cpu_ids() is set (such as in static functions)
     * This will destroy all data
     */
    void resize(unsigned int max_cpu_id,T v) {
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
     * get_value can be used to iterate around all per-thread vairables.
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
    inline size_t weight() {
        return _size;
    }

    inline unsigned int get_mapping(int i) {
        return i;
    }
protected:
    AT* storage;
    size_t _size;
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
 * need a table to map thread ids to posiitions inside the storage
 * vector. The mapping table itself is a vector of integers, so
 * it your data structure is not bigger than two ints, it is not worth it.
 */
template <typename T>
class per_thread_omem { private:
    typedef struct {
        T v;
    } CLICK_CACHE_ALIGN AT;

    AT* storage;
    Vector<unsigned int> mapping;
    size_t _size;
public:
    per_thread_omem() {
    }
    inline size_t size() {
        return _size;
    }
    void compress(Bitvector usable) {
        storage = new AT[usable.weight()];
        int id = 0;
        for (int i = 0; i < click_max_cpu_ids(); i++) {
            if (usable[i]) {
                mapping[i] = id++;
            } else {
                mapping[i] = 0;
            }
        }
        _size = id;
    }


    void initialize(Bitvector usable, T v=T()) {
        storage = new AT[usable.weight()];
        int id = 0;
        for (int i = 0; i < click_max_cpu_ids(); i++) {
            if (usable[i]) {
                storage[id] = v;
                mapping[i] = id++;
            } else {
                mapping[i] = 0;
            }
        }
        _size = id;
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

};

CLICK_ENDDECLS
#endif
