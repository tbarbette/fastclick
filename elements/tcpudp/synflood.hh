#ifndef CLICK_SYNFLOOD_HH
#define CLICK_SYNFLOOD_HH

#include <click/config.h>
#include <click/batchelement.hh>
#include <click/task.hh>
#include <click/timer.hh>
#include <click/notifier.hh>

CLICK_DECLS


/**
 * =c
 * SYNFlood(SRCIP, DSTPORT,I<keywords>)
 * 
 *
 * =s tcp
 *
 * Generates a SYNflood
 *
 * =d
 *
 * Sequentially emit TCP SYN packets with increasing source port and IP.
 * The 5-tuple space will be scanned in a round-robin fashion.
 * The output packets are ethernet packets with the addresses set to 0.
 *
 * The source port will be increased until it will reach the 0 value.
 * Then, it will start from a new source IP and generates SYNs for all the ports.
 * When the IP space is finished, it will wrap and start from 0.0.0.1
 *
 *
 * Keywords:
 * =item SRCIP 
 * Initial Source IP
 * =item DSTIP
 * Destination IP
 * =item SPORT
 * Source port. Default is 1000
 * =item DPORT
 * Initial destination port. Default is 80
 * =item STOP
 * Stop the driver when the limit is reached. Default true.
 * =item ACTIVE
 * If false, then do not emit packets until the `active' handler is written.
 * Default is true.
 * =item BURST
 * How many packets to generate per each iteration. Default is 32.
 * =item LIMIT
 * Limit the total number of packets to generate. Default is -1 (no limit).
 * The effective number of packets emitted will be the smallest multiple of BURST after LIMIT.
 * 
 * =item LEN
 * The lenght of the generated packets. Default is 60
 * 
 * =e
 *
 *  SYNFlood(10.1.1.1, 172.16.1.1, 1, 80, LEN 1400)
 *  -> EtherRewrite(11:11:11:11:11:11, 22:22:22:22:22:22)
 *  -> ToDPDKDevice(0)
 * 
 *
 **/


class SYNFlood : public BatchElement {
  public:
    SYNFlood() CLICK_COLD;
    ~SYNFlood() CLICK_COLD;

    const char *class_name() const override { return "SYNFlood"; }
    const char *port_count() const override { return PORTS_0_1; }
    // const char *processing() const override	{ return PUSH;}

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) override CLICK_COLD;
    void add_handlers() override CLICK_COLD;

    bool run_task(Task *) override;

    Packet *get_packet(bool push = true);
    Packet *pull(int) override;
#if HAVE_BATCH
    PacketBatch *pull_batch(int, unsigned) override;
#endif
    void run_timer(Timer *timer) override;

  private:
    int _active;
    int _stop;
    int64_t _limit;
    Task _task;
    ActiveNotifier _notifier;
    Timer _timer;

    unsigned _burst;

    struct in_addr _sipaddr;
    struct in_addr _dipaddr;
    uint16_t _sport;
    uint16_t _dport;
    uint16_t _len = 60;

    static String read_handler(Element *, void *) CLICK_COLD;
    static int write_handler(const String &, Element *, void *,
                             ErrorHandler *) CLICK_COLD;
};

CLICK_ENDDECLS
#endif
