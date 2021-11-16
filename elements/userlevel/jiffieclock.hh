#ifndef CLICK_JiffieClock_HH
#define CLICK_JiffieClock_HH
#include <click/element.hh>
#include <click/timer.hh>
#include <click/task.hh>
#include <click/sync.hh>
CLICK_DECLS

/* =c
 * JiffieClock()
 * =s
 * Aggregate jiffies at CLICK_HZ frequency instead of relying on TSC.
 *
 */

class JiffieClock : public Element { public:

  JiffieClock() CLICK_COLD;

  const char *class_name() const override        { return "JiffieClock"; }
  const char *port_count() const override        { return PORTS_0_0; }

  int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
  int initialize(ErrorHandler *) CLICK_COLD;

  void run_timer(Timer*);
  bool run_task(Task*);

  static String read_param(Element *e, void *thunk);
  void add_handlers();

  static click_jiffies_t read_jiffies(void* user);
private:
  Task _task;
  Timer _timer;
  Timestamp last_jiffies_update;
  click_jiffies_t jiffies;
  bool _verbose;
  int _minprecision;
};

CLICK_ENDDECLS
#endif
