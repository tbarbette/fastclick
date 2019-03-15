#ifndef CLICK_FlowWebGen_HH
#define CLICK_FlowWebGen_HH
#include <click/glue.hh>
#include <click/task.hh>
#include <click/timer.hh>
#include <click/ipaddress.hh>

#include <click/flow/flowelement.hh>
CLICK_DECLS

class TCPClientAck;

/*
 * =c
 * FlowWebGen(PREFIX, DST, RATE)
 * =s app
 * =d
 * Ask for a random web pages over and over with repeated HTTP
 * connections. Generate them with random source IP addresses
 * starting with PREFIX, and destination address DST.
 * =e
 * kt :: KernelTap(11.11.0.0/16);
 * kt -> Strip(14)
 *    -> FlowWebGen(11.11.0.0/16, 10.0.0.1)
 *    -> EtherEncap(0x0800, 1:1:1:1:1:1, 2:2:2:2:2:2)
 *    -> kt;
 */

class FlowWebGen : public FlowElement {
 public:

  FlowWebGen() CLICK_COLD;
  ~FlowWebGen() CLICK_COLD;

  const char *class_name() const		{ return "FlowWebGen"; }
  const char *port_count() const		{ return PORTS_1_1; }
  const char *processing() const		{ return PUSH; }
  int initialize(ErrorHandler *) CLICK_COLD;
  void cleanup(CleanupStage) CLICK_COLD;
  int configure(Vector<String> &conf, ErrorHandler *errh) CLICK_COLD;

  PacketBatch *simple_action_batch(PacketBatch *);

  void run_timer(Timer *);
  bool run_task(Task *);

private:
  Timer _timer;
  IPAddress _src_prefix;
  IPAddress _mask;
  IPAddress _dst;
	int _limit;
    bool _active;
    String _url;
    int _verbose;

    Task _task;
    int _parallel;
  atomic_uint32_t _id;
  TCPClientAck* _tcp_client_ack;

  // TCP Control Block
  class CB {
  public:
    CB() CLICK_COLD;

    IPAddress _src;		// Our IP address.
    unsigned short _sport;	// network byte order.
    unsigned short _dport;

    unsigned _iss;
    unsigned _snd_una;
    unsigned _snd_nxt;
    unsigned _irs;
    unsigned _rcv_nxt;

    unsigned char
	_connected:1,		// Got SYN+ACK
	_got_fin:1,		// Got FIN
	_sent_fin:1,		// Sent FIN
	_closed:1,		// Got ACK for our FIN
	_do_send:1,
	_spare_bits:3;
    char _resends;

    Timestamp last_send;
    char sndbuf[64];
    unsigned sndlen;

    void reset (IPAddress src, String url);

    void remove_from_list ();
    void add_to_list (CB **phead);

    void rexmit_unlink ();
    void rexmit_update (CB *tail);

    CB *next;
    CB **pprev;

/*    CB *rexmit_next;
    CB *rexmit_prev;*/
  };

  static const int htbits = 10;
  static const int htsize = 1 << htbits;
  static const int htmask = htsize - 1;
  CB *cbhash[htsize];
  CB *cbfree;
//  CB *rexmit_head, *rexmit_tail;

  // Retransmission
  static const int resend_dt = 1000000;	// rexmit after 1 sec
  static const int resend_max = 5;	// rexmit at most 5 times

  // Scheduling new connections
  int start_interval;			// ms between connections
  Timestamp start_tv;

  // Performance measurement
  static const int perf_dt = 5000000;
  Timestamp perf_tv;
  struct {
    int initiated;
    int completed;
    int reset;
    int timeout;
  } perfcnt;

  void do_perf_stats ();

  void recycle(CB *);
  CB *find_cb(unsigned src, unsigned short sport, unsigned short dport);
  IPAddress pick_src();
  int connhash(unsigned src, unsigned short sport);

  WritablePacket *fixup_packet (Packet *p, unsigned plen);

  Packet* tcp_input(Packet *);
  Packet* tcp_send(CB *, Packet *);
  WritablePacket* tcp_output(WritablePacket *p,
	IPAddress src, unsigned short sport,
	IPAddress dst, unsigned short dport,
	int seq, int ack, char tcpflags,
	char *payload, int paylen);

    void set_active(bool active);
    static int write_handler(const String & s_in, Element *e, void *thunk, ErrorHandler *errh);
    void add_handlers();


};

CLICK_ENDDECLS
#endif
