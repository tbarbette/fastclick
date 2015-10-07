#ifndef CLICK_FROMDPDKDEVICE_HH
#define CLICK_FROMDPDKDEVICE_HH

#include <click/batchelement.hh>
#include <click/notifier.hh>
#include <click/task.hh>
#include <click/dpdkdevice.hh>
#include "queuedevice.hh"

CLICK_DECLS

/*
 * =title FromDpdkDevice
 *
 * =c
 *
 * FromDpdkDevice(DEVNAME [, PROMISC])
 *
 * =s netdevices
 *
 * reads packets from network device using Intel's DPDK (user-level)
 *
 * =d
 *
 * Reads packets from the network controller named DEVNAME, using Intel's DPDK.
 *
 * On the contrary to FromDevice.u which acts as a sniffer by default, packets
 * received by devices put in DPDK mode will NOT be received by the kernel, and
 * will thus be processed only once.
 *
 * =item DEVNAME
 *
 * String.  Device number
 *
 * =item PROMISC
 *
 * Boolean.  FromDpdkDevice puts the device in promiscuous mode if PROMISC is
 * true. The default is false.
 *
 * =item BURST
 *
 * Maximal number of packets that will be processed before rescheduling Click
 *  default is 32.
 *
 * =item MAXTHREADS
 *
 * Maximal number of threads that this element will take to read packets from
 * 	the input queue. If unset (or negative) all threads not pinned with a
 * 	ThreadScheduler element will be shared among FromDPDKDevice elements and
 *  other input elements supporting multiqueue (extending QueueDevice)
 *
 * =item THREADOFFSET
 *
 * Specify which Click thread will handle this element. If multiple
 *  j threads are used, threads with id THREADOFFSET+j will be used. Default is
 *  to share the threads available on the device's NUMA node equally.
 *
 * =item MINQUEUE
 *
 * Minimum number of hardware queue of the devices to use. Multiple queues
 *  allows to load balance the traffic on multiple thread using RSS.
 *  Default is 1.
 *
 * =item MAXQUEUES
 *
 * Maximum number of hardware queue to use. Default is 128.
 *
 * =item NDESC
 *
 * Number of descriptor per ring. If unset (or negative), default amount will be
 *  used.
 *
 * =back
 *
 * This element is only available at user level, when compiled with DPDK
 * support.
 *
 * =e
 *
 *   FromDpdkDevice() ->
 *
 * =n
 *
 * FromDpdkDevice sets packets' extra length annotations as appropriate.
 *
 * =h count read-only
 *
 * Returns the number of packets read by the device.
 *
 * =h reset_counts write-only
 *
 * Resets "count" to zero.
 *
 * =a ToDpdkDevice
 */

class FromDpdkDevice : public QueueDevice {
public:

    FromDpdkDevice() CLICK_COLD;
    ~FromDpdkDevice() CLICK_COLD;

    const char *class_name() const    { return "FromDpdkDevice"; }
    const char *port_count() const    { return PORTS_0_1; }
    const char *processing() const    { return PUSH; }
    int configure_phase() const {
        return CONFIGURE_PHASE_PRIVILEGED - 5;
    }
    bool can_live_reconfigure() const { return false; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;
    void add_handlers() CLICK_COLD;
    void cleanup(CleanupStage stage) CLICK_COLD;
    bool run_task(Task *);
    
private:

    static String read_handler(Element*, void*) CLICK_COLD;
    static int write_handler(const String&, Element*, void*, ErrorHandler*)
        CLICK_COLD;


    unsigned _port_no;
    bool _promisc;
    unsigned int _burst;
    bool _set_rss_aggregate;
};

CLICK_ENDDECLS

#endif // CLICK_FROMDPDKDEVICE_HH
