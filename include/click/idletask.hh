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


public:

	IdleTask(Element* e) : _e(e), _next(0) {
	}

	inline bool fire() {
		return _e->run_idle_task(this);
	}

	void initialize(Element *owner, int tid)
	{
	    Router *router = owner->router();
		RouterThread *thread = router->master()->thread(tid);
		_next = thread->_idletask;
		thread->_idletask = this;
		thread->wake();
	}

	friend class RouterThread;
};

CLICK_ENDDECLS
#endif
