#ifndef CLICK_LINUXCLOCK_HH
#define CLICK_LINUXCLOCK_HH
#include <click/element.hh>

CLICK_DECLS

/* =c
 * LinuxClock()
 * =s
 * Use Linux to get time
 * =d
 *
 * Click needs to be compiled with --enable-user-timestamp for this to be
 * used. This Clock can be used as a BASE of TSCClock
 *
 */

class LinuxClock : public Element, UserClock { public:

  LinuxClock() CLICK_COLD;

  const char *class_name() const override        { return "LinuxClock"; }
  const char *port_count() const override        { return PORTS_0_0; }

  void* cast(const char *name);
  int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
  int initialize(ErrorHandler *) CLICK_COLD;
  void cleanup(CleanupStage);

  int64_t now(bool steady) override;

  enum {h_now, h_now_steady};
  static String read_handler(Element *e, void *thunk);
  void add_handlers() CLICK_COLD;

private:
  int _verbose;
  bool _install;
};


CLICK_ENDDECLS
#endif
