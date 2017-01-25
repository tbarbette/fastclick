// -*- c-basic-offset: 4 -*-
#ifndef CLICK_ALLOCATOR_HH
#define CLICK_ALLOCATOR_HH

#include <click/config.h>
#include <click/glue.hh>
#include <click/multithread.hh>
#include <click/sync.hh>

CLICK_DECLS

/**
 * Multithread-safe pool allocator.
 *
 * There is one pool per thread, which can contains up to POOL_SIZE (default 64)
 * objects.
 *
 * If this pool is full, it will be moved to a global list (protected
 * by a lock). The global list will contain at most POOL_COUNT pools.
 *
 * If the global list limit is reached, the objects will be directly
 *  released.
 *
 * Works mostly like the packet allocator.
 */
template <typename T, int POOL_SIZE = 64, int POOL_COUNT = 32>
class pool_allocator_mt { public:

    typedef struct item_t{
        struct item_t* next;
    } item;

    typedef struct Pool_t {
        Pool_t() {
            first = 0;
            count = 0;
        }
        item* first;
        int count;
    } Pool;

    typedef struct GlobalPool_t {
        uint8_t _pad[sizeof(T*)];
        struct GlobalPool_t* next;
    } GlobalPool;

    pool_allocator_mt();
    ~pool_allocator_mt();

    T* allocate(const T &t) {
#if CLICK_DEBUG_ALLOCATOR
        _allocated++;
#endif
        T* e;
        Pool& p = _pool.get();
        if (p.count == 0) {
            _global_lock.acquire();
            if (_global_count > 0) {
#if CLICK_DEBUG_ALLOCATOR
                click_chatter("Global pool retrieved by thread %d",click_current_cpu_id());
#endif
                p.first = (item*)_global_pool;
                p.count = POOL_SIZE;
                _global_pool = _global_pool->next;
                _global_count--;
            }
            _global_lock.release();
            if (!p.first) {
                p.first = (item*)(CLICK_LALLOC(sizeof(T)));
#if CLICK_DEBUG_ALLOCATOR
                _newed++;
#endif
                p.first->next = 0;
                p.count++;
            }
        }
        e = (T*)p.first;
        p.first = p.first->next;
        p.count--;
        new(e) T(t);
        return e;
    }

    void release(T* e) {
#if CLICK_DEBUG_ALLOCATOR
        _released++;
#endif
        e->~T();
        Pool& p = _pool.get();
        if (unlikely(p.count++ == POOL_SIZE)) {
            _global_lock.acquire();
            if (_global_count == POOL_COUNT-1) {
                _global_lock.release();
                CLICK_LFREE(e,sizeof(T));
#if CLICK_DEBUG_ALLOCATOR
                click_chatter("Global pool is full, freeing item");
#endif
            } else {
                //Move current pool to global pool
#if CLICK_DEBUG_ALLOCATOR
                click_chatter("Pool for thread %d is full, putting the pool in the global list",click_current_cpu_id());
#endif
                _global_count++;
                GlobalPool* new_pool = (GlobalPool*)(p.first);
                new_pool->next = _global_pool;
                _global_pool = new_pool;
                _global_lock.release();
                p.count = 1;
                ((item*)e)->next = 0;
                p.first = (item*)e;
            }
        } else {
            ((item*)e)->next = p.first;
            p.first = (item*)e;
        }
    }
private:

    per_thread<Pool> _pool;
    Spinlock _global_lock;
    int _global_count;
    GlobalPool* _global_pool;


#if CLICK_DEBUG_ALLOCATOR
    atomic_uint32_t _allocated;
    atomic_uint32_t _released;
    atomic_uint32_t _newed;
#endif
};


template <typename T, int POOL_SIZE, int POOL_COUNT>
pool_allocator_mt<T,POOL_SIZE,POOL_COUNT>::pool_allocator_mt() :_global_count(0),_global_pool(0),_pool(Pool()) {
#if CLICK_DEBUG_ALLOCATOR
    _released = 0;
    _allocated = 0;
    _newed = 0;
#endif
}

template <typename T, int POOL_SIZE, int POOL_COUNT>
pool_allocator_mt<T,POOL_SIZE,POOL_COUNT>::~pool_allocator_mt() {
        static_assert(sizeof(T) >= sizeof(Pool), "Allocator object is too small");
        int n_release = 0;
        while (_global_pool) {
            item* p = (item*)_global_pool;
            _global_pool = _global_pool->next;
            item* next;
            while (p) {
                next = p->next;
                CLICK_LFREE(p,sizeof(T));
                p = next;
                n_release++;
            }
        }
        for (int i = 0 ; i < _pool.weight(); i++) {
            Pool &pool = _pool.get_value(i);
            item* p = pool.first;
            item* next;
            while (p) {
                next = p-> next;
                CLICK_LFREE(p,sizeof(T));
                p = next;
                n_release++;
            }
        }
#if CLICK_DEBUG_ALLOCATOR
        click_chatter("Allocator : Newed %u, Freed %d, allocate() %u, released() %u",_newed,n_release,_allocated,_released);
#endif
    }

CLICK_ENDDECLS
#endif
