#ifndef CLICK_FROMDPDKDEVICE_HH
#define CLICK_FROMDPDKDEVICE_HH

#include <click/batchelement.hh>
#include <click/notifier.hh>
#include <click/task.hh>
#include <click/dpdkdevice.hh>
#include "queuedevice.hh"

CLICK_DECLS

/*
=title FromDPDKDevice

=c

FromDPDKDevice(PORT [, QUEUE, N_QUEUES, I<keywords> PROMISC, BURST, NDESC])

=s netdevices

reads packets from network device using Intel's DPDK (user-level)

=d

Reads packets from the network device with DPDK port identifier PORT.

On the contrary to FromDevice.u which acts as a sniffer by default, packets
received by devices put in DPDK mode will NOT be received by the kernel, and
will thus be processed only once.

To use RSS (Receive Side Scaling) to receive packets from the same device
on multiple queues (possibly pinned to different Click threads), simply
use multiple FromDPDKDevice with the same PORT argument. Each
FromDPDKDevice will open a different RX queue attached to the same port,
and packets will be dispatched among the FromDPDKDevice elements that
you can pin to different thread using StaticThreadSched.

Arguments:

=over 8

=item PORT

Integer or PCI address.  Port identifier of the device, or a PCI address in the
format fffff:ff:ff.f

=item QUEUE

Integer.  A specific hardware queue to use. Default is 0.

=item N_QUEUES

Integer.  Number of hardware queues to use. -1 or default is to use as many queues as threads assigned to this element.

=item PROMISC

Boolean.  FromDPDKDevice puts the device in promiscuous mode if PROMISC is
true. The default is false.

=item BURST

Integer.  Maximal number of packets that will be processed before rescheduling.
The default is 32.

=item MAXTHREADS

Maximal number of threads that this element will take to read packets from
the input queue. If unset (or negative) all threads not pinned with a
ThreadScheduler element will be shared among FromDPDKDevice elements and
other input elements supporting multiqueue (extending QueueDevice)

=item THREADOFFSET

Specify which Click thread will handle this element. If multiple
j threads are used, threads with id THREADOFFSET+j will be used. Default is
to share the threads available on the device's NUMA node equally.

=item NDESC

Integer.  Number of descriptors per ring. The default is 256.

=item ALLOW_NONEXISTENT

Boolean.  Do not fail if the PORT do not existent. If it's the case the task
will never run and this element will behave like Idle.

=item ACTIVE
Boolean. If False, the device is only initialized. Use this when you want
to read packet using secondary DPDK applications.

=back

This element is only available at user level, when compiled with DPDK
support.

=e

  FromDPDKDevice(3, QUEUE 1) -> ...

=h count read-only

Returns the number of packets read by the device.

=h reset_count write-only

Resets "count" to zero.

=a DPDKInfo, ToDPDKDevice */

class FromDPDKDevice : public RXQueueDevice {
public:

    FromDPDKDevice() CLICK_COLD;
    ~FromDPDKDevice() CLICK_COLD;

    const char *class_name() const { return "FromDPDKDevice"; }
    const char *port_count() const { return PORTS_0_1; }
    const char *processing() const { return PUSH; }
    int configure_phase() const {
        return CONFIGURE_PHASE_PRIVILEGED - 5;
    }
    bool can_live_reconfigure() const { return false; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;
    void add_handlers() CLICK_COLD;
    void cleanup(CleanupStage) CLICK_COLD;
    bool run_task(Task *);
    
private:

    static String read_handler(Element*, void*) CLICK_COLD;
    static int write_handler(const String&, Element*, void*, ErrorHandler*)
        CLICK_COLD;
    static String status_handler(Element *e, void * thunk) CLICK_COLD;
    static String statistics_handler(Element *e, void * thunk) CLICK_COLD;
    enum {
        h_vendor, h_driver, h_carrier, h_duplex, h_autoneg, h_speed,
        h_ipackets, h_ibytes, h_ierrors, h_idropped, h_active, h_mac
    };

    DPDKDevice* _dev;
    bool _active;
};

CLICK_ENDDECLS

#endif // CLICK_FROMDPDKDEVICE_HH
