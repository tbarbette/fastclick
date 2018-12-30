// -*- c-basic-offset: 4 -*-
#ifndef CLICK_TONETMAPDEVICE_HH
#define CLICK_TONETMAPDEVICE_HH
#include <click/config.h>
#include <click/element.hh>
#include <click/task.hh>
#include <click/timer.hh>
#include <click/notifier.hh>
#include <click/etheraddress.hh>
#include <click/netmapdevice.hh>
#include <click/multithread.hh>
#include <vector>
#include "queuedevice.hh"

CLICK_DECLS

/*
 * =title ToNetmapDevice
 *
 * =c
 *
 * ToNetmapDevice(DEVNAME [, QUEUE, N_QUEUES, [, I<keywords> PROMISC, BURST])
 *
 * Sends packets using Netmap
 *
 * =s comm
 *
 * =d
 *
 * Send packets through a Netmap device, optionnaly specifying a queue number.
 * This element is NOT thread safe. This element supports both push and pull.
 * In push mode, it will batch packets up to BURST then sends them. If the
 * output rings are full, it will either block until there is some space (if
 *  BLOCKANT is set) or it will internally queue the packets and finally drops
 *  them if there is still not enough space.
 *
 * =item DEVNAME
 *
 * String Device name
 *
 *=item QUEUE
 *
 * Integer.  A specific hardware queue to use. Default is 0.
 *
 *=item N_QUEUES
 *
 * Integer.  Number of hardware queues to use. -1 or default is to use as many
 * queues as threads which can end up in this element.
 *
 * =item IQUEUE (push mode only)
 *
 * Unsigned integer Number of packets that we can bufferize if all output rings are full while in push mode
 *
 * =item BLOCKING (push mode only)
 *
 * Boolean. If true and packets are pushed and the IQUEUE is full, the element will loop until there is space in the output ring, or we'll drop. Default true.
 *
 * =item MAXTHREADS
 *
 * Maximum number of threads to use.
 *
 * =item BURST
 *
 * Number of packets to batch before sending them out.
 *
 * =h count read-only
 *
 * Returns the number of packets sent by the device.
 *
 * =h dropped read-only
 *
 * Returns the number of packets dropped by the device.
 *
 * =h reset_counts write-only
 *
 * Resets n_send and n_dropped counts to zero.
 *
 */


class ToNetmapDevice: public TXQueueDevice  {

public:
	ToNetmapDevice() CLICK_COLD;

    void selected(int, int);

    const char *class_name() const		{ return "ToNetmapDevice"; }
    const char *port_count() const		{ return "1/0-2"; }
    const char *processing() const		{ return "a/h"; }
    const char *flags() const			{ return "S2"; }
    int	configure_phase() const			{ return CONFIGURE_PHASE_PRIVILEGED; }

    void allow_txsync();
    void try_txsync(int queue, int fd);

    void do_txsync(int fd);
    void do_txreclaim(int fd);

    void cleanup(CleanupStage);
    int configure(Vector<String>&, ErrorHandler*) CLICK_COLD;
    int initialize(ErrorHandler*) CLICK_COLD;

    void add_handlers() CLICK_COLD;

#if HAVE_BATCH
    void push_batch(int port, PacketBatch*);
#endif
    void push(int, Packet*);
    void run_timer(Timer *timer);

    bool run_task(Task *);
    static String toqueue_read_rate_handler(Element *e, void *thunk);

  protected:

    unsigned long last_count;
    unsigned long last_rate;
    std::vector<Timer*> _zctimers;
    std::vector<bool> _iodone;
    bool _debug;
    enum { h_signal };

    bool _pull_use_select;

    unsigned int send_packets(Packet* &packet, bool ask_sync=false, bool txsync_on_empty = true);

    NetmapDevice* _device;

    class State {
    public:
        State() : backoff(0), q(NULL), q_size(0), last_queue(0), timer() {};
        unsigned int backoff;
    	Packet* q; //Pending packets to send
	int q_size;
    	unsigned int last_queue; //Last queue used to send packets
    	NotifierSignal signal;
       Timer* timer; //Timer for repeateadly empty pull()
    };
    per_thread<State> state;
};

CLICK_ENDDECLS
#endif
