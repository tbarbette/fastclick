#ifndef CLICK_SIMPLESPINLOCK_HH
#define CLICK_SIMPLESPINLOCK_HH
#include <click/element.hh>
#include <click/sync.hh>
CLICK_DECLS

/*
 * =c
 * PathSpinlock(LOCK)
 * =s threads
 * acquires a spinlock, push/pull the packets then release the lock
 * =d
 * Acquires the spinlock named LOCK. LOCK must be defined in a SpinlockInfo
 * element.
 * In a push context, the packet is then pushed through the pipeline and when the
 * processing down the push path is done, releases the lock.
 * In a pull contextn, the pacjet us then pulled and before being returned, the lock is released.
 *
 * =n
 * Ensure that a push path or a pull part will not be traversed by multiple threads at the same
 * time while SpinlockAcquire/SpinlockRelease work for any path but do not
 * support dropping packets between a SpinlockAcquire and a SpinlockRelease
 *
 * =a SpinlockInfo, SpinlockAcquire, SpinlockRelease
 */

class PathSpinlock : public Element { public:

    PathSpinlock()			: _lock(0) {}
    ~PathSpinlock()			{}

    const char *class_name() const	{ return "PathSpinlock"; }
    const char *port_count() const	{ return "1-/="; }
    const char *processing() const	{ return AGNOSTIC; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push(int,Packet *p);
    Packet* pull(int);

  private:
    bool _lock_release;
    Spinlock *_lock;

};

CLICK_ENDDECLS
#endif
