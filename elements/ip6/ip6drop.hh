#ifndef CLICK_IP6_DROP_HH
#define CLICK_IP6_DROP_HH
#include <click/batchelement.hh>
#include <click/element.hh>
#include <click/glue.hh>
#include <click/atomic.hh>
#include <clicknet/ip6.h>
#include <click/ip6address.hh>

CLICK_DECLS

/*
=c

IP6Drop(ADDR[, ADDR, ...], P, R, K, H)

=s ip

Gilbert-Elliott drop model

=d

=e


  IP6SRv6FECDecode(fc00::a, fc00::9)

=a IP6Encap */


class IP6Drop : public BatchElement { 
 
 public:

  IP6Drop();
  ~IP6Drop();

  const char *class_name() const override        { return "IP6Drop"; }
  const char *port_count() const override        { return PORTS_1_1; }

  int  configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
  bool can_live_reconfigure() const     { return true; }
  void add_handlers() CLICK_COLD;
  static String read_handler(Element *e, void *thunk) CLICK_COLD;

  void push(int, Packet *p_in) override;
#if HAVE_BATCH
  void push_batch(int, PacketBatch * batch_in) override;
#endif
  Packet *drop_model(Packet *p_in);

  bool gemodel();
  bool uniform_model();
  bool addr_eq(uint32_t *a1, uint32_t *a2);
  bool deterministic_drop();

private:

  Vector<IP6Address> addrs;

  enum state_e {
    good,
    bad
  };

  struct Stats {
    Stats() : total_seen(0), total_drop(0), total_drop_source(0), total_received(0) {

    }

    uint64_t total_seen; // Number of analyzed packets
    uint64_t total_drop;
    uint64_t total_drop_source;
    uint64_t total_received;
  };

  struct Shared {
    Shared() : state(good) {

    }
     uint64_t de_count;
    state_e state; // State of the machine
  };
  SimpleSpinlock _lock;
  per_thread<Stats> _stats;
  Shared _protected;


  double p; // Good -> bad
  double r; // Bad -> good
  double h; // Don't drop in bad
  double k; // Don't drop in good

  uint16_t seed;
  bool is_uniform;
  double uniform_drop;
  bool is_in_good;
  uint32_t nb_burst;
  bool is_deterministic;
};

CLICK_ENDDECLS
#endif


