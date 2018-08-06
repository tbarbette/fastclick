// -*- c-basic-offset: 4 -*-
#ifndef CLICK_FROMNETMAPDEVICE_HH
#define CLICK_FROMNETMAPDEVICE_HH
#include <click/config.h>
#include <click/task.hh>
#include <click/etheraddress.hh>
#include <click/netmapdevice.hh>
#include "queuedevice.hh"
#include <vector>
#include <stdint.h>
#include <dirent.h>
#include <fcntl.h>
#include <iostream>
#include <fstream>

CLICK_DECLS

/*
 * =c
 *
 * FromNetmapDevice(DEVNAME [, QUEUE, NR_QUEUE, [, I<keywords> PROMISC, BURST])
 *
 * Receives packets using Netmap
 *
 * =s netdevices
 *
 * =d
 *
 * =item DEVNAME
 *
 * String.  Device number
 *
 *=item QUEUE
 * Integer.  A specific hardware queue to use. Default is 0.
 *
 *=item N_QUEUES
 *
 * Integer.  Number of hardware queues to use. -1 or default is to use all
 * available queues, as RSS is probably enabled and packet will come to all RX
 * queues. Be noticed that using more than 1 queue per RX thread is useless
 * in 99% of cases and you should use ethtool -L [interface] combined N or
 * equivalent to ensure that.
 *
 * =item PROMISC
 *
 * Boolean.  FromNetmapDevice puts the device in promiscuous mode if PROMISC is
 * true. The default is false.
 *
 * =item BURST
 *
 * Unsigned integer. Maximal number of packets that will be processed before
 *  rescheduling Click default is 32.
 *
 * =item MAXTHREADS
 *
 * Maximal number of threads that this element will take to read packets from
 * 	the input queue. If unset (or negative) all threads not pinned with a
 * 	ThreadScheduler element will be shared among FromNetmapDevice elements and
 *  other input elements supporting multiqueue (extending QueueDevice)
 *
 * =item THREADOFFSET
 *
 * Define a number of assignable threads to ignore and do not use. Assignable
 * threads are the one on the same numa node than this device (if NUMA is true)
 *  and not assigned to other elements using StaticThreadSched. Default is
 *  to share the threads available on the device's NUMA node equally.
 *
 * =item VERBOSE
 *
 * Amount of verbosity. If 1, display warnings about potential misconfigurations. If 2, display some informations. Default to 1.
 *
 *
 */



class FromNetmapDevice: public RXQueueDevice {

public:

    FromNetmapDevice() CLICK_COLD;

    void selected(int, int);

    const char *class_name() const		{ return "FromNetmapDevice"; }
    const char *port_count() const		{ return PORTS_0_1; }
    const char *processing() const		{ return PUSH; }

    int configure_phase() const			{ return CONFIGURE_PHASE_PRIVILEGED - 5; }
    void* cast(const char*);


    int configure(Vector<String>&, ErrorHandler*) CLICK_COLD;
    int initialize(ErrorHandler*) CLICK_COLD;
    void cleanup(CleanupStage) CLICK_COLD;

    inline bool receive_packets(Task* task, int begin, int end, bool fromtask);

    bool run_task(Task *);

  protected:

    NetmapDevice* _device;

    //Do not quit the task until we have sended all possible packets (until all queues are empty)
    bool _keephand;

    std::vector<int> _queue_for_fd;

    int queue_for_fd(int fd) {
        return _queue_for_fd[fd];
    }

    void add_handlers();

    String read_handler(Element *e, void *);

    int write_handler(const String &, Element *e, void *,
                                      ErrorHandler *);

};

CLICK_ENDDECLS
#endif
