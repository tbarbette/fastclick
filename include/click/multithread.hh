// -*- c-basic-offset: 4 -*-
#ifndef CLICK_MULTITHREAD_HH
#define CLICK_MULTITHREAD_HH

#include <click/glue.hh>
#include <click/vector.hh>
#include <click/bitvector.hh>

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
public:
    per_thread() {
        _size = click_max_cpu_ids();
        storage = new AT[_size];
    }

    per_thread(T v) {
        initialize(click_max_cpu_ids(),v);
    }

    per_thread(T v, int n) {
        initialize(n,v);
    }

    ~per_thread() {
        delete[] storage;
    }

    /**
     * Resize must be called if per_thread was initialized before click_max_cpu_ids() is set (such as in static functions)
     * This will destroy all data
     */
    void resize(unsigned int n,T v) {
        delete[] storage;
        initialize(n,v);
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

    inline T& get_value_for_thread(int i) const{
        return storage[i].v;
    }

    inline void set_value_for_thread(int i, T v) {
            storage[i].v = v;
    }

    inline size_t size() {
        return _size;
    }

    inline T& get_value(int i) const{
        return storage[i].v;
    }

    inline void set_value(int i, T v) {
        storage[i].v = v;
    }

    inline unsigned int get_mapping(int i) {
        return i;
    }
protected:
    AT* storage;
    size_t _size;
};

/*
 * Not compressed for now, just avoid parcouring unused threads
 */
template <typename T>
class per_thread_compressed : public per_thread<T> {

public:
    void compress(Bitvector v) {
        usable = v;
        mapping.resize(v.weight(),0);
        int id = 0;
        for (int i = 0; i < usable.size(); i++) {
            if (usable[i]) {
                mapping[id] = i;
                id++;
            }
        }
        _size = id;
    }

    inline size_t size() {
        return _size;
    }

    inline T& get_value(int i) const{
        return per_thread<T>::storage[mapping[i]].v;
    }

    inline void set_value(int i,T v) {
        per_thread<T>::storage[mapping[i]].v = v;
    }

    inline unsigned int get_mapping(int i) {
        return mapping[i];
    }

    Bitvector usable;
    Vector<unsigned int> mapping;
    size_t _size;

};

CLICK_ENDDECLS
#endif
