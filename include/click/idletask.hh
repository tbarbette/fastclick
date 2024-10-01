// -*- c-basic-offset: 4 -*-
#ifndef CLICK_IDLETASK_HH
#define CLICK_IDLETASK_HH
#include <click/element.hh>
#include <click/router.hh>
#include <click/master.hh>
#include <click/sync.hh>
#if HAVE_MULTITHREAD
# include <click/atomic.hh>
# include <click/ewma.hh>
#endif
CLICK_DECLS

class IdleTask {
	Element* _e;
	IdleTask* _next;
	bool _active = true;
	Timestamp _due;
	int _min_interval;


public:

	IdleTask(Element* e) : _e(e), _next(0), _active(false), _due(0),_min_interval(0) {
	}

    inline bool is_due(Timestamp& now) {
        return likely(_active && (!_min_interval || now >= _due));

    }

	inline bool fire(Timestamp& now) {
	    if (_min_interval)
	        _due = now + Timestamp::make_msec(_min_interval);
		return _e->run_idle_task(this);
	}

	void initialize(Element *owner, int tid, int min_msec = 0)
	{
	    Router *router = owner->router();
		RouterThread *thread = router->master()->thread(tid);
		_min_interval = min_msec;
        _active = true;
		_next = thread->_idletask;
		thread->_idletask = this;
        thread->_idle_dorun = 0;
		thread->wake();
	}

	friend class RouterThread;
};

CLICK_ENDDECLS
#endif
