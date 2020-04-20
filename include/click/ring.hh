// -*- c-basic-offset: 4 -*-
#ifndef CLICK_RING_HH
#define CLICK_RING_HH

#include <click/atomic.hh>
#include <click/sync.hh>
#if HAVE_DPDK
# include <click/algorithm.hh>
# include <rte_ring.h>
# include <rte_errno.h>
# include <click/dpdk_glue.hh>
#endif
#include <type_traits>

CLICK_DECLS

    template <typename T, size_t RING_SIZE> class BaseRing {

    protected:

    inline bool has_space() {
        return head - tail < RING_SIZE;
    }

    inline bool is_empty() {
        return head == tail;
    }

    public:
    int id;
        BaseRing() {
            head = 0;
            tail = 0;
        }


        inline T extract() {
            if (!is_empty()) {
                T &v = ring[tail % RING_SIZE];
                tail++;
                return v;
            } else
                return 0;
        }

        inline bool insert(T batch) {
            if (has_space()) {
                ring[head % RING_SIZE] = batch;
                head++;
                return true;
            } else
                return false;
        }

        inline unsigned int count() {
            return head - tail;
        }

        T ring[RING_SIZE];
        uint32_t head;
        uint32_t tail;


        void pool_transfer(unsigned int thread_from, unsigned int thread_to) {

        }

        inline void hint(unsigned int num, unsigned int thread_id) {

        }


    };


/**
 * Ring with size set at initialization time
 *
 * NOT MT-Safe
 */
template <typename T> class SPSCDynamicRing {

protected:
    uint32_t _size;

	inline uint32_t next_i(uint32_t i) {
		if (i == _size -1)
			return 0;
		else
			return i + 1;
	}

	inline void inc_i(uint32_t &i) {

		if (i == _size -1 )
			i = 0;
		else
			i++;
	}

    T* ring;
public:
    SPSCDynamicRing() : _size(0),ring(0) {
        head = 0;
        tail = 0;
    }

    ~SPSCDynamicRing() {
    	if (_size)
    		delete[] ring;
    }


    inline T extract() {
        if (!is_empty()) {
            T &v = ring[tail];
            inc_i(tail);
            return v;
        } else
            return 0;
    }

    inline bool insert(T batch) {
        if (!is_full()) {
            ring[head] = batch;
            inc_i(head);
            return true;
        } else
            return false;
    }

    inline unsigned int count() {
        int count = (int)head - (int)tail;
        if (count < 0)
            count += _size;
        return count;
    }

    inline bool is_empty() {
        return head == tail;
    }

    inline bool is_full() {
        return next_i(head) == tail;
    }


    uint32_t head;
    uint32_t tail;

    inline bool initialized() {
        return _size > 0;
    }

    inline void initialize(int size, const char* = 0) {
        _size = size;
        ring = new T[size];
    }
};

template <typename T>
using DynamicRing = SPSCDynamicRing<T>;

#if HAVE_DPDK
/**
 * Ring with size set at initialization time
 */
template <typename T> class MPMCDynamicRing {
    protected:
    rte_ring* _ring;
    public:
    MPMCDynamicRing() : _ring(0) {
        static_assert(std::is_pointer<T>(), "MPMCDynamicRing can only be used with pointers");
    }

    inline void initialize(int size, const char* name, int flags = 0) {
        assert(!_ring);
        _ring = rte_ring_create(name, next_pow2(size), SOCKET_ID_ANY, flags);
        if (unlikely(_ring == 0)) {
            click_chatter("Could not create DPDK ring error %d: %s",rte_errno,rte_strerror(rte_errno));
        }
    }

    inline bool insert(T o) {
        return rte_ring_mp_enqueue(_ring, (void*)o) == 0;
    }

    inline T extract() {
        T o;

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
        if (rte_ring_mc_dequeue_bulk(_ring, (void**)&o, 1, 0) == 0)
#else
        if (rte_ring_mc_dequeue_bulk(_ring, (void**)&o, 1) == 0)
#endif
            return 0;
        else
            return o;
    }


    inline bool is_empty() {
        return rte_ring_empty(_ring);
    }

    inline bool is_full() {
        return rte_ring_full(_ring);
    }


    ~MPMCDynamicRing() {
        rte_ring_free(_ring);
    }
};


template <typename T> class MPSCDynamicRing : public MPMCDynamicRing<T> {
    public:

    inline void initialize(int size, const char* name) {
        MPMCDynamicRing<T>::initialize(size, name, RING_F_SC_DEQ);
    }

    inline T extract() {
        T o;
#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
        if (rte_ring_sc_dequeue_bulk(this->_ring, (void**)&o, 1, 0) == 0)
#else
        if (rte_ring_sc_dequeue_bulk(this->_ring, (void**)&o, 1) == 0)
#endif
            return 0;
        else
            return o;
    }
};
#else
/**
 * Ring with size set at initialization time
 *
 * NOT MT-Safe
 */
template <typename T> class MPMCDynamicRing : public SPSCDynamicRing<T> {
	Spinlock _lock;

public:

	MPMCDynamicRing() : _lock() {

	}

    inline T extract() {
        _lock.acquire();
        T v= SPSCDynamicRing<T> :: extract();
        _lock.release();
        return v;
    }

    inline bool insert(T v) {
	_lock.acquire();
	bool r = SPSCDynamicRing<T> :: insert(v);
	_lock.release();
	return r;
    }

};
template <typename T> class MPSCDynamicRing : public MPMCDynamicRing<T> {};

#endif


template <typename T, size_t RING_SIZE> class Ring : public BaseRing<T,RING_SIZE> {};

template <typename T> class CircleList {
public:
    CircleList() : _data(0), _maxsize(0),_size(0),_cur(0) {

    }

    CircleList(unsigned int max_size) : _data(0), _maxsize(max_size),_size(0),_cur(0) {

    }

    inline T& get() {
        return _data[_cur % _size];
    }

    inline T& next() {
        return _data[++_cur % _size];
    }

    inline void advance() {
        ++_cur;
    }

    void append(T v) {
        assert(_size < _maxsize);
        if (!_data)
            _data = new T[_maxsize];
        _data[_size++] = v;
    }

    unsigned int size() {
        return _size;
    }

    inline T& begin() {
        return _data[0];
    }

    inline bool is_empty() {
        return _size == 0;
    }
    private :
    T* _data;
    unsigned int _size;
    unsigned int _maxsize;
    unsigned int _cur;
};

#define SPSCRing Ring

template <typename T, size_t MAX_SIZE> class MPMCLIFO {
private:
    SimpleSpinlock _lock;

protected:

	inline bool has_space() {
		return _count < MAX_SIZE;
	}

	inline bool is_empty() {
	   return _first == 0;
	}

public:
	int id;
	T _first;
	unsigned int _count;

    MPMCLIFO() : _lock(), id(0),_first(0),_count(0) {

    }


    inline T extract() {
        _lock.acquire();
        if (is_empty()) {
            _lock.release();
            return 0;
        }
        T tmp = _first;
        _first = _first->prev();
        _count--;
        _lock.release();
        return tmp;
    }

    inline bool insert(T v) {
        _lock.acquire();
        if (!has_space()) {
            _lock.release();
            return false;
        }
        v->set_prev(_first);
        _first = v;
        _count++;
        _lock.release();
        return true;
    }

    inline unsigned int count() {
        return _count;
    }


    void pool_transfer(unsigned int thread_from, unsigned int thread_to) {

    }

    inline void hint(unsigned int num, unsigned int thread_id) {

    }
};


template <typename T, size_t RING_SIZE> class MPMCRing : public SPSCRing<T, RING_SIZE> {
    Spinlock _lock;


public:
    inline void acquire() {
        _lock.acquire();
    }

    inline void release() {
        _lock.release();
    }

    inline void release_tail() {
        release();
    }

    inline void release_head() {
        release();
    }

    inline void acquire_tail() {
        acquire();
    }

    inline void acquire_head() {
        acquire();
    }

public:

    MPMCRing() : _lock() {

    }

    inline bool insert(T batch) {
        acquire_head();
        if (SPSCRing<T,RING_SIZE>::has_space()) {
            SPSCRing<T,RING_SIZE>::ring[SPSCRing<T,RING_SIZE>::head % RING_SIZE] = batch;
            SPSCRing<T,RING_SIZE>::head++;
            release_head();
            return true;
        } else {
            release_head();
            return false;
        }
    }

    inline T extract() {
        acquire_tail();
        if (!SPSCRing<T,RING_SIZE>::is_empty()) {
            T &v = SPSCRing<T,RING_SIZE>::ring[SPSCRing<T,RING_SIZE>::tail % RING_SIZE];
            SPSCRing<T,RING_SIZE>::tail++;
            release_tail();
            return v;
        } else {
            release_tail();
            return 0;
        }
    }
};

template <typename T, size_t RING_SIZE> class SMPMCRing : public SPSCRing<T, RING_SIZE> {
    SimpleSpinlock _lock_head;
    SimpleSpinlock _lock_tail;

    inline void release_tail() {
        _lock_tail.release();
    }

    inline void release_head() {
        _lock_head.release();
    }

    inline void acquire_tail() {
        _lock_tail.acquire();
    }

    inline void acquire_head() {
        _lock_head.acquire();
    }
public:
    inline bool insert(T batch) {
        acquire_head();
        if (SPSCRing<T,RING_SIZE>::has_space()) {
            SPSCRing<T,RING_SIZE>::ring[SPSCRing<T,RING_SIZE>::head % RING_SIZE] = batch;
            SPSCRing<T,RING_SIZE>::head++;
            release_head();
            click_compiler_fence();
            return true;
        } else {
            release_head();
            click_compiler_fence();
            return false;
        }
    }

    inline T extract() {
        acquire_tail();
        if (!SPSCRing<T,RING_SIZE>::is_empty()) {
            T &v = SPSCRing<T,RING_SIZE>::ring[SPSCRing<T,RING_SIZE>::tail % RING_SIZE];
            SPSCRing<T,RING_SIZE>::tail++;
            release_tail();
            click_compiler_fence();
            return v;
        } else {
            release_tail();
            click_compiler_fence();
            return 0;
        }
    }

};

template <typename T, size_t RING_SIZE> class MPSCRing : public SPSCRing<T, RING_SIZE> {
    SimpleSpinlock _lock_head;

    inline void release_head() {
        _lock_head.release();
    }

    inline void acquire_head() {
        _lock_head.acquire();
    }
public:

    inline bool insert(T batch) {
        acquire_head();
        if (SPSCRing<T,RING_SIZE>::has_space()) {
            SPSCRing<T,RING_SIZE>::ring[SPSCRing<T,RING_SIZE>::head % RING_SIZE] = batch;
            SPSCRing<T,RING_SIZE>::head++;
            release_head();
            return true;
        } else {
            release_head();
            return false;
        }
    }
};

CLICK_ENDDECLS
#endif
