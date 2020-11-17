#ifndef CLICK_MBGCLOCK_HH
#define CLICK_MBGCLOCK_HH
#include <click/element.hh>
#include <click/timer.hh>
#include <click/task.hh>
#include <click/sync.hh>
#include <click/timestamp.hh>
#undef HAVE_CONFIG_H //Clash with Meinberg's same define
#include <mbgdevio.h>
#include <mbgpccyc.h>

CLICK_DECLS

/* =c
 * MBGClock()
 * =s
 * Use meinberg device to get time
 * =d
 *
 * Click needs to be compiled with --enable-user-timestamp for this to be used.
 *
 */

class MBGClock : public Element,UserClock { public:

  MBGClock() CLICK_COLD;

  const char *class_name() const override        { return "MBGClock"; }
  const char *port_count() const override        { return PORTS_0_0; }

  int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
  int initialize(ErrorHandler *) CLICK_COLD;
  void cleanup(CleanupStage);

  void* cast(const char *name);

  int64_t now(bool steady=false) override;

  uint64_t cycles();

  enum {h_now, h_cycles, h_now_steady};
  static String read_handler(Element *e, void *thunk);
  void add_handlers() CLICK_COLD;

private:
  int _verbose;
  bool _install;
  bool _comp;
  MBG_DEV_HANDLE _dh;
};


CLICK_ENDDECLS
#endif
