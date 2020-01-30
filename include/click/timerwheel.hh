#ifndef CLICK_TIMERWHEEL_HH
#define CLICK_TIMERWHEEL_HH 1
CLICK_DECLS

template <typename T>
class TimerWheel {
    uint32_t _mask;
    uint32_t _index;
    Vector<T*> _buckets;
    Spinlock _writers_lock;

public:
    TimerWheel() : _index(0) {

    }

    void initialize(int max) {
        max = next_pow2(max + 2);
        _mask = max - 1;
        _buckets.resize(max);
    }

    inline void schedule_after(T* obj, uint32_t timeout, const std::function<void(T*,T*)> setter) {
        unsigned id = ((*(volatile uint32_t*)&_index) + timeout) & _mask;
        T* f = _buckets.unchecked_at(id);
        setter(obj,f);
        _buckets.unchecked_at(id) = obj;
    }

    inline void schedule_after_mp(T* obj, uint32_t timeout, const std::function<void(T*,T*)> setter) {
        _writers_lock.acquire();
        unsigned id = ((*(volatile uint32_t*)&_index) + timeout) & _mask;

        //click_chatter("Enqueue %p at %d", obj, id);
        T* f = _buckets.unchecked_at(id);
        setter(obj,f);
        _buckets.unchecked_at(id) = obj;
        //click_write_fence(); done by release()
        _writers_lock.release();
    }

    /**
     * Must be called by one thread only!
     */
    inline void run_timers(std::function<T*(T*)> expire) {
        T* f = _buckets.unchecked_at((_index) & _mask);
            //click_chatter("Expire %d -> %d", _index, _index & _mask);
        while (f != 0) {
            f = expire(f);
        }
            _buckets.unchecked_at((_index) & _mask) = 0;
        _index++;
    }
};

CLICK_ENDDECLS
#endif
