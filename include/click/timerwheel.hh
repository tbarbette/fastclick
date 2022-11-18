#ifndef CLICK_TIMERWHEEL_HH
#define CLICK_TIMERWHEEL_HH 1

#include <click/algorithm.hh>

CLICK_DECLS

template <typename T>
class TimerWheel {
    public:
        TimerWheel() : _index(0) {
        }

        void initialize(int max) {
            max = next_pow2(max + 2);
            _mask = max - 1;
            _buckets.resize(max, 0);
        }

        /**
         * Schedule @a obj for deletion in @a timeout epochs.
         * @pre timeout > 0 and <= max given epochs at initialization
         */
        inline void schedule_after(T* obj, uint32_t timeout, const std::function<void(T*,T*)> setter) {
            assert(timeout > 0); //Likely a bug
            assert(timeout < _mask);
            unsigned id = ((*(volatile uint32_t*)&_index) + timeout) & _mask;

            //click_chatter("Enqueue %p at %d", obj, id);
            T* f = _buckets.unchecked_at(id);
            setter(obj,f);

            _buckets.unchecked_at(id) = obj;
        }

        inline void schedule_after_mp(T* obj, uint32_t timeout, const std::function<void(T*,T*)> setter) {
            _writers_lock.acquire();
            unsigned id = ((*(volatile uint32_t*)&_index) + timeout) & _mask;

            //click_chatter("Enqueuemp %p at %d", obj, id);
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
            //click_chatter("Expire %d -> %d (_mask %d)", _index, _index & _mask, _mask);
            while (f != 0) {
                f = expire(f);
            }
            _buckets.unchecked_at((_index) & _mask) = 0;
            _index++;
        }

        bool debug_find(T*obj, std::function<T*(T*)> next ) {
            int id = _index;
            for (int i =0; i <= _mask; i++) {
                T* a = _buckets[(id + i) & _mask];
                while(a) {
                if (a == obj)
                    return true;
                a = next(a);
                }
            }
            return false;
        }

    private:
        uint32_t _mask;
        uint32_t _index;
        Vector<T*> _buckets;
        Spinlock _writers_lock;
};

CLICK_ENDDECLS
#endif
