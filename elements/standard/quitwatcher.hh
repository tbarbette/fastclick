#ifndef CLICK_QUITWATCHER_HH
#define CLICK_QUITWATCHER_HH
#include <click/element.hh>
#include <click/timer.hh>
CLICK_DECLS
class Handler;

/*
=c

QuitWatcher(ELEMENT, ...)

=s control

stops router processing

=d

Stops router processing when at least one of the ELEMENTs is no longer
scheduled.

*/

class QuitWatcher : public Element { public:

  QuitWatcher() CLICK_COLD;

  const char *class_name() const override		{ return "QuitWatcher"; }
  int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
  int initialize(ErrorHandler *) override CLICK_COLD;

  void run_timer(Timer *) override;

 private:

  Vector<Element*> _e;
  Vector<const Handler*> _handlers;
  Timer _timer;

};

CLICK_ENDDECLS
#endif
