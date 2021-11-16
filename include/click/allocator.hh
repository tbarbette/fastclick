// -*- c-basic-offset: 4 -*-
#ifndef CLICK_ALLOCATOR_HH
#define CLICK_ALLOCATOR_HH

#include <click/config.h>
#include <click/glue.hh>
#include <click/multithread.hh>
#include <click/sync.hh>
#include <typeinfo>
#define CLICK_DEBUG_ALLOCATOR 0

CLICK_DECLS

#define ALLOCATOR_POISON 0xA2C188F1

class pool_allocator_mt_base {
public:
        /**
         * Avoid warnings when final releasing which frees a lot of objects
         */
        static void set_dying(bool v) {
            _dying = v;
        }
        static bool dying() {
            return _dying;
        }
private:
        static bool _dying;
protected:
        static int _n_msg;
};

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
 * Works mostly like the Click packet allocator.
 *
 * This version use casting and reuses the memory from the items themselves
 * to build linked list of pool. Use pool_allocator_aware_mt with virtual
 * classes to avoid overwriting vtable pointer, by exposing a pool_next_item
 * and pool_next_pool void* pointer to use instead of cast.
 *
 * Template arguments :
 * @arg zero : Zero-initialize elements, do not set this to true if T is a class that will be constructed. Use this only if calling allocate_unitialized
 * @arg pool_size
 * @arg pool_count
 *
 */
template <typename T, bool zero = false, int POOL_SIZE = 64, int POOL_COUNT = 32>
class pool_allocator_mt : pool_allocator_mt_base { public:

    typedef struct item_t{
        struct item_t* next;
#if CLICK_DEBUG_ALLOCATOR
        uint32_t poison;
#endif
    } item;

    typedef struct Pool_t {
        Pool_t() {
            first = 0;
            count = 0;
        }
        item* first;
        int count;

        int find_count() {
            int i = 0;
            auto p = first;
            while (p != 0) {
                p = p->next;
                i++;
            }
            return i;
        }
    } Pool;

    typedef struct GlobalPool_t {
        uint8_t _pad[sizeof(item)];
        struct GlobalPool_t* next;
    } GlobalPool;

    pool_allocator_mt();
    ~pool_allocator_mt();

    void static_initialize() {
        _pool.resize(click_max_cpu_ids(),Pool());
    }

    T* allocate_uninitialized() {
#if CLICK_DEBUG_ALLOCATOR
        _allocated++;
        assert(_allocated >= _released);
#endif
        T* e;
        Pool& p = _pool.get();
#if CLICK_DEBUG_ALLOCATOR
        assert(p.count == p.find_count());
#endif
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
                if (zero) {
                    bzero(p.first,sizeof(T));
                }
#if CLICK_DEBUG_ALLOCATOR
                _newed++;
#endif
                p.first->next = 0;
                p.count++;
            } else {
#if CLICK_DEBUG_ALLOCATOR
                assert( ((item*)(p.first))->poison == ALLOCATOR_POISON);
                ((item*)(p.first))->poison = 0;
#endif
            }
        } else {
#if CLICK_DEBUG_ALLOCATOR
                assert( ((item*)(p.first))->poison == ALLOCATOR_POISON);
                ((item*)(p.first))->poison = 0;
#endif
        }
        e = (T*)p.first;
        p.first = p.first->next;
        p.count--;
#if CLICK_DEBUG_ALLOCATOR
        assert(p.count == p.find_count());
#endif

#if CLICK_DEBUG_ALLOCATOR > 1
        click_chatter("%s Allocate %p %d %d",  typeid(T).name(), e,_released, _allocated);
#endif
        return e;
    }

    T* allocate(const T &t) {
        T* e = allocate_uninitialized();
        new(e) T(t);
        return e;
    }

    T* allocate() {
        T* e = allocate_uninitialized();
        new(e) T();
        return e;
    }

    void release_unitialized(T* e) {

#if CLICK_DEBUG_ALLOCATOR
        _released++;
#if CLICK_DEBUG_ALLOCATOR > 1
        click_chatter("%s Release %p %d %d", typeid(T).name(), e, _released, _allocated);
#endif

        assert(_released <= _allocated);
#endif
        Pool& p = _pool.get();
        ++p.count;
#if CLICK_DEBUG_ALLOCATOR
        assert(((item*)e)->poison != ALLOCATOR_POISON);
        ((item*)e)->poison = ALLOCATOR_POISON;
#endif
        if (unlikely(p.count == POOL_SIZE + 1)) {
            _global_lock.acquire();
            if (_global_count == POOL_COUNT-1 || dying()) {
                _global_lock.release();
                CLICK_LFREE(e,sizeof(T));
                if (!dying()) {
#if CLICK_DEBUG_ALLOCATOR
                    click_chatter("Global pool is full, freeing item");
#endif
		    if (_n_msg++ < 5)
			click_chatter("Extremly inefficient pool_allocator_mt for %s ! Change parameters !",typeid(T).name());
                }
                p.count --;
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
#if CLICK_DEBUG_ALLOCATOR
        assert(p.count == p.find_count());
#endif
    }

    void release(T* e) {
        e->~T();
        release_unitialized(e);
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

template <typename T, int POOL_SIZE = 64, int POOL_COUNT = 32>
class pool_allocator_aware_mt : pool_allocator_mt_base { public:

    typedef struct Pool_t {
        Pool_t() {
            first = 0;
            count = 0;
        }
        T* first;
        int count;

        int find_count() {
            int i = 0;
            T* p = first;
            while (p != 0) {
                p = (T*)p->pool_next_item;
                i++;
            }
            return i;
        }
    } Pool;


    pool_allocator_aware_mt();
    ~pool_allocator_aware_mt();

    bool initialized() {
        return _pool.weight() == click_max_cpu_ids();
    }

    T* allocate_uninitialized() {
#if CLICK_DEBUG_ALLOCATOR
        _allocated++;
        assert(_allocated >= _released);
#endif
        T* e;
        Pool& p = _pool.get();
#if CLICK_DEBUG_ALLOCATOR
        assert(p.count == p.find_count());
#endif
        if (p.count == 0) {
            _global_lock.acquire();
            if (_global_count > 0) {
#if CLICK_DEBUG_ALLOCATOR
                click_chatter("Global pool retrieved by thread %d",click_current_cpu_id());
#endif
                p.first = _global_pool;
                p.count = POOL_SIZE;
                _global_pool = (T*)_global_pool->pool_next_pool;
                _global_count--;
            }
            _global_lock.release();
            if (!p.first) {
                p.first =  new T();
#if CLICK_DEBUG_ALLOCATOR
                _newed++;
#endif
                p.first->pool_next_item = 0;
                p.count++;
            } else {
#if CLICK_DEBUG_ALLOCATOR
                assert(p.first->poison == ALLOCATOR_POISON);
                p.first->poison = 0;
#endif
            }
        } else {
#if CLICK_DEBUG_ALLOCATOR
                assert(p.first->poison == ALLOCATOR_POISON);
                p.first->poison = 0;
#endif
        }
        e = p.first;
        p.first = (T*)p.first->pool_next_item;
        p.count--;
#if CLICK_DEBUG_ALLOCATOR
        assert(p.count == p.find_count());
#endif
        return e;
    }

    void release_unitialized(T* e) {
#if CLICK_DEBUG_ALLOCATOR
        _released++;
        assert(_released <= _allocated);
#endif
        Pool& p = _pool.get();
        ++p.count;
#if CLICK_DEBUG_ALLOCATOR
        assert(e->poison != ALLOCATOR_POISON);
        e->poison = ALLOCATOR_POISON;
#endif
        if (unlikely(p.count == POOL_SIZE + 1)) {
            _global_lock.acquire();
            if (_global_count == POOL_COUNT-1 || dying()) {
                _global_lock.release();
                delete e;
                if (!dying()) {
#if CLICK_DEBUG_ALLOCATOR
                    click_chatter("Global pool is full, freeing item");
#endif
                    click_chatter("Extremly inefficient pool_allocator_mt for %s ! Change parameters !",typeid(T).name());
                }
                p.count --;
            } else {
                //Move current pool to global pool
#if CLICK_DEBUG_ALLOCATOR
                click_chatter("Pool for thread %d is full, putting the pool in the global list",click_current_cpu_id());
#endif
                _global_count++;
                T* new_pool = (T*)(p.first);
                new_pool->pool_next_pool = _global_pool;
                _global_pool = new_pool;
                _global_lock.release();
                p.count = 1;
                e->pool_next_item = 0;
                p.first = (T*)e;
            }
        } else {
            e->pool_next_item = p.first;
            p.first = e;
        }
#if CLICK_DEBUG_ALLOCATOR
        assert(p.count == p.find_count());
#endif
    }

private:

    per_thread<Pool> _pool;
    Spinlock _global_lock;
    int _global_count;
    T* _global_pool;


#if CLICK_DEBUG_ALLOCATOR
    atomic_uint32_t _allocated;
    atomic_uint32_t _released;
    atomic_uint32_t _newed;
#endif
};


template <typename T, bool ZERO, int POOL_SIZE, int POOL_COUNT>
pool_allocator_mt<T,ZERO,POOL_SIZE,POOL_COUNT>::pool_allocator_mt() : _pool(Pool()), _global_count(0),_global_pool(0) {
#if CLICK_DEBUG_ALLOCATOR
    _released = 0;
    _allocated = 0;
    _newed = 0;
#endif
}

template <typename T, bool ZERO, int POOL_SIZE, int POOL_COUNT>
pool_allocator_mt<T,ZERO,POOL_SIZE,POOL_COUNT>::~pool_allocator_mt() {
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
        for (unsigned i = 0 ; i < _pool.weight(); i++) {
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


template <typename T,int POOL_SIZE, int POOL_COUNT>
pool_allocator_aware_mt<T,POOL_SIZE,POOL_COUNT>::pool_allocator_aware_mt() : _pool(Pool()),_global_count(0),_global_pool(0) {
#if CLICK_DEBUG_ALLOCATOR
    _released = 0;
    _allocated = 0;
    _newed = 0;
#endif
}

template <typename T, int POOL_SIZE, int POOL_COUNT>
pool_allocator_aware_mt<T,POOL_SIZE,POOL_COUNT>::~pool_allocator_aware_mt() {
        static_assert(sizeof(T) >= sizeof(Pool), "Allocator object is too small");
        int n_release = 0;
        while (_global_pool) {
            T* p = _global_pool;
            _global_pool = (T*)_global_pool->pool_next_pool;
            T* next;
            while (p) {
                next = (T*)p->pool_next_item;
                delete p;
                p = next;
                n_release++;
            }
        }
        for (unsigned i = 0 ; i < _pool.weight(); i++) {
            Pool &pool = _pool.get_value(i);
            T* p = pool.first;
            T* next;
            while (p) {
                next = (T*)p->pool_next_item;
                delete p;
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
