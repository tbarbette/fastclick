// -*- c-basic-offset: 4 -*-
#ifndef CLICK_MULTITHREAD_HH
#define CLICK_MULTITHREAD_HH

#include <click/config.h>

CLICK_DECLS

extern int nthreads;

#if CLICK_USERLEVEL && HAVE_MULTITHREAD
#  define GET_CPU_ID() click_current_thread_id
#else
#  define GET_CPU_ID() click_current_processor()
#endif

template <typename T>
class per_thread
{
	typedef struct {
		T v;
	} __attribute__((aligned(64))) AT;

	void initialize(unsigned int n, T v) {
	    _size = n;
        storage = new AT[_size];
        for (int i = 0; i < n; i++) {

            storage[i].v = v;

        }
	}
public:
	per_thread() {
	    _size = nthreads;
		storage = new AT[_size];
	}

	per_thread(T v) {
	    initialize(nthreads,v);
	}

	per_thread(T v, int n) {
	    initialize(n,v);
	}

	/**
	 * Resize must be called if per_thread was initialized before nthreads is set (such as in static functions)
	 * This will destroy all data
	 */
	void resize(unsigned int n,T v) {
	    delete[] storage;
	    initialize(n,v);
	}

	~per_thread() {
		delete[] storage;
	}
    //explicit per_thread(void (*cleanup_function)(T*));


	inline T* operator->() const {
        return &(storage[GET_CPU_ID()].v);
    }
    inline T& operator*() const {
        return storage[GET_CPU_ID()].v;
    }

    inline T& operator+=(const T& add) const {
        storage[GET_CPU_ID()].v += add;
        return storage[GET_CPU_ID()].v;
    }

    inline T& operator++() const { // prefix ++
        return ++(storage[GET_CPU_ID()].v);
    }

    inline T operator++(int) const { // postfix ++
        return storage[GET_CPU_ID()].v++;
    }

    inline T& operator--() const {
        return --(storage[GET_CPU_ID()].v);
    }

    inline T operator--(int) const {
        return storage[GET_CPU_ID()].v--;
    }

    inline T& operator=(T value) const {
        storage[GET_CPU_ID()].v = value;
        return storage[GET_CPU_ID()].v;
    }

    inline void set(T v) {
        storage[GET_CPU_ID()].v = v;
    }

    inline void setAll(T v) {
        for (int i = 0; i < nthreads; i++)
            storage[i].v = v;
    }

    inline T& get() const{
        return storage[GET_CPU_ID()].v;
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

CLICK_ENDDECLS
#endif
