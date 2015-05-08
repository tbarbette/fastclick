// -*- c-basic-offset: 4 -*-
#ifndef CLICK_TONETMAPDEVICE_HH
#define CLICK_TONETMAPDEVICE_HH
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
 * =c
 *
 * ToNetmapDevice(DEVNAME)
 *
 * =item DEVNAME
 *
 * String Device name
 *
 */


class ToNetmapDevice: public QueueDevice  {

public:
	std::vector <Packet*> _queues;
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

    void push(int, Packet*);

    void run_timer(Timer *timer);

    bool run_task(Task *);
    static String toqueue_read_rate_handler(Element *e, void *thunk);

  protected:


    unsigned int _burst;
    bool _block;
    unsigned long last_count;
    unsigned long last_rate;
    std::vector<Timer*> _zctimers;
    std::vector<bool> _iodone;
    bool _debug;
    enum { h_signal };

    unsigned int _internal_queue;

    unsigned int send_packets(Packet* &packet, bool push);

    NetmapDevice* _device;

    class State {
    public:
        State() : backoff(0), q(NULL), q_size(0), last_queue(0) {};
        unsigned int backoff;
    	Packet* q; //Pending packets to send
    	unsigned int q_size;
    	unsigned int last_queue; //Last queue used to send packets
    	NotifierSignal signal;
    };
    per_thread<State> state;
};

CLICK_ENDDECLS
#endif
