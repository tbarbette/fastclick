// -*- c-basic-offset: 4 -*-
#ifndef STATICTHREADSCHED_HH
#define STATICTHREADSCHED_HH
#include <click/element.hh>
#include <click/standard/threadsched.hh>
CLICK_DECLS

/*
 * =c
 * StaticThreadSched(ELEMENT THREAD, ...)
 * =s threads
 * specifies element and thread scheduling parameters
 * =d
 * Statically binds elements to threads. If more than one StaticThreadSched
 * is specified, they will all run. The one that runs later may override an
 * earlier run.
 *
 * If Click is compiled with NUMA support (libnuma was found at configure time) one can use the format socket/core such as 1/3 which will use the 3rd core of the first NUMA socket.
 *
 * =a
 * ThreadMonitor, BalancedThreadSched
 */

class StaticThreadSched : public Element, public ThreadSched { public:

    StaticThreadSched() CLICK_COLD;
    ~StaticThreadSched() CLICK_COLD;

    const char *class_name() const override	{ return "StaticThreadSched"; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    int initial_home_thread_id(const Element *e);

    Bitvector assigned_thread();

  private:
    Vector<int> _thread_preferences;
    ThreadSched *_next_thread_sched;

    bool set_preference(int eindex, int preference);
};

CLICK_ENDDECLS
#endif
