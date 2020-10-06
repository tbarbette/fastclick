#ifndef CLICK_DPDKDEVCLOCK_HH
#define CLICK_DPDKDEVCLOCK_HH
#include "tscclock.hh"
#include <click/dpdkdevice.hh>

CLICK_DECLS

/* =c
 * DPDKDeviceClock()
 * =s
 * Use DPDK device to get time
 * =d
 *
 * Click needs to be compiled with --enable-user-timestamp for this to be used.
 *
 */

class DPDKDeviceClock : public TSCClock { public:

  DPDKDeviceClock() CLICK_COLD;
  ~DPDKDeviceClock() CLICK_COLD;

  const char *class_name() const override        { return "DPDKDeviceClock"; }

  int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
  int initialize(ErrorHandler *) CLICK_COLD;

  DPDKDevice* _dev;
};


CLICK_ENDDECLS
#endif //CLICK_DPDKDEVCLOCK_HH
