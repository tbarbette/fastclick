#ifndef CLICK_FROMDPDKDEVICE_HH
#define CLICK_FROMDPDKDEVICE_HH

#include <click/batchelement.hh>
#include <click/notifier.hh>
#include <click/task.hh>
#include <click/dpdkdevice.hh>
#include "queuedevice.hh"
#include "../../vendor/nicscheduler/ethernetdevice.hh"

CLICK_DECLS

/*
=title FromDPDKDevice

=c

FromDPDKDevice(PORT [, QUEUE, N_QUEUES, I<keywords> PROMISC, BURST, NDESC])

=s netdevices

reads packets from network device using DPDK (user-level)

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

=over 20

=item PORT

Integer or PCI address. Port identifier of the device or a PCI address in the
format fffff:ff:ff.f

=item QUEUE

Integer. A specific hardware queue to use. Default is 0.

=item N_QUEUES

Integer. Number of hardware queues to use. -1 or default is to use as many queues
as threads assigned to this element.

=item PROMISC

Boolean. FromDPDKDevice puts the device in promiscuous mode if PROMISC is
true. The default is false.

=item BURST

Integer. Maximal number of packets that will be processed before rescheduling.
The default is 32.

=item MAXTHREADS

Integer. Maximal number of threads that this element will take to read packets from
the input queue. If unset (or negative) all threads not pinned with a
ThreadScheduler element will be shared among FromDPDKDevice elements and
other input elements supporting multiqueue (extending QueueDevice)

=item THREADOFFSET

Integer. Specify which Click thread will handle this element. If multiple
j threads are used, threads with id THREADOFFSET+j will be used. Default is
to share the threads available on the device's NUMA node equally.

=item NDESC

Integer. Number of descriptors per ring. The default is 256.

=item MAC

Colon-separated string. The device's MAC address.

=item MTU

Integer. The maximum transfer unit of the device.

=item MODE

String. The device's Rx mode. Can be none, rss, vmdq, vmdq_rss,
vmdq_dcb, vmdq_dcb_rss. For DPDK version >= 20.02, 'flow' is also
supported (DPDK's Flow API) if FastClick was built with --enable-flow-api.

=item FLOW_RULES_FILE

String. For DPDK version >= 20.02, FastClick was built with --enable-flow-api,
and if MODE is set to flow, a path to a file with Flow Rule Manager rules
can be supplied to the device. These rules are installed in the NIC using
DPDK's flow API.

=item FLOW_ISOLATE

Boolean. Requires MODE flow. Isolated mode guarantees that all ingress
traffic comes from defined flow rules only (current and future).
If ingress traffic does not match any of the defined rules, it will be
discarded by the NIC. Defaults to false.

=item VF_POOLS

Integer. The number of virtual function pools to be used by VMDq.

=item VF_VLAN

Vector of Integers. Contains the VLAN tags to be used for dispatching input
traffic using VLAN-based VMDq.

=item PAUSE

String. Set the device pause mode. "full" to enable pause frame for both
RX and TX, "rx" or "tx" to set one of them, and "none" to disable pause frames.
Do not set or choose "unset" to keep device current state/default.

=item ALLOW_NONEXISTENT

Boolean.  Do not fail if the PORT does not exist. If it's the case the task
will never run and this element will behave like Idle.

=item RSS_AGGREGATE

Boolean. If True, sets the RSS hash into the aggregate annotation
field of each packet. Defaults to False.

=item PAINT_QUEUE

Boolean. If True, sets the hardware queue number into the paint annotation
field of each packet. Defaults to False.

=item NUMA

Boolean. If True, allocates CPU cores in a NUMA-aware fashion.

=item NUMA_NODE

Integer. Specify the NUMA node to undertake packet processing.

=item RX_INTR

Integer. Enables Rx interrupts if non-negative value is given.
Defaults to -1 (no interrupts).

=item SCALE

String. Defines the scaling policy. Can be parallel or share.
Scaling is disabled by default.

=item TIMESTAMP

Boolean. Enables hardware timestamping. Defaults to false.

=item VLAN_FILTER

Boolean. Per queue ability to filter received VLAN packets by the hardware. Defaults to false.

=item VLAN_STRIP

Boolean. Per queue ability to strip VLAN header by the hardware in received VLAN packets. Defaults to false.

=item VLAN_EXTEND

Boolean. Per queue ability to extend VLAN tagged packets via QinQ. Defaults to false.

=item LRO

Boolean. Enables hardware-based Large Receive Offloading (LRO).
When set to true, the NIC coalesces consecutive frames of the same flow into larger frames.
Defaults to false.

=item JUMBO

Boolean. Enables the reception of Jumbo frames by the hardware. Defaults to false.

=item ACTIVE

Boolean. If False, the device is only initialized. Use this when you want
to read packet using secondary DPDK applications.

=item VERBOSE

Boolean. If True, more detailed messages about the device are printed to
the stdout. Defaults to False.

=back

This element is only available at user level, when compiled with DPDK
support.

=e

  FromDPDKDevice(3, QUEUE 1) -> ...

=h device read-only

Returns the device number.

=h duplex read-only

Returns whether link is duplex (1) or not (0) for an active link (status is up).

=h autoneg read-only

Returns whether device supports auto negotiation.

=h speed read-only

Returns the device's link speed in Mbps.

=h carrier read-only

Returns the device's link status (1 for link up, otherwise 0).

=h type read-only

Returns the device's link type (only fiber is currently supported).

=h xstats read-only

Returns a device's detailed packet and byte counters.
If a parameter is given, only the matching counter will be returned.

=h queue_count read-only

Returns the number of used descriptors of a specific Rx queue.
If no queue is specified, all queues' used descriptors are returned.

=h queue_packets read-only

Returns the number of packets of a specific Rx queue.
If no queue is specified, all queues' packets are returned.

=h queue_bytes read-only

Returns the number of bytes of a specific Rx queue.
If no queue is specified, all queues' bytes are returned.

=h rule_packet_hits read-only

Returns the number of packets matched by a specific rule.
If no rule number is specified, an error is returned (aggregate statistics not supported).

=h rule_byte_count read-only

Returns the number of bytes matched by a specific rule.
If no rule number is specified, an error is returned (aggregate statistics not supported).

=h rules_aggr_stats read-only

Returns aggregate rule statistics.

=h active read-only

Returns the status of the device (1 for active, otherwise 0).

=h active/safe_active write-only

Sets the status of the device (1 for active, otherwise 0).

=h count read-only

Returns the number of packets read by the device.

=h reset_counts write-only

Resets "count" to zero.

=h reset_load write-only

Resets load counters to zero.

=h nb_rx_queues read-only

Returns the number of Rx queues of this device.

=h nb_tx_queues read-only

Returns the number of Tx queues of this device.

=h nb_vf_pools read-only

Returns the number of Virtual Function pools of this device.

=h nb_rx_desc read-only

Returns the number of Rx descriptors of this device.

=h mac read-only

Returns the Ethernet address of this device.

=h vendor read-only

Returns the vendor of this device.

=h driver read-only

Returns the driver of this device.

=h add_mac write-only

Sets the Ethernet address of this device.

=h remove_mac write-only

Removes the Ethernet address of this device.

=h vf_mac_addr read-only

Returns the Ethernet addresses of the Virtual Functions of this device.
If JSON is supported, the return format is JSON, otherwise a string is returned.

=h max_rss write-only

Reconfigures the size of the RSS table.

=h hw_count read-only

Returns the number of packets received by this device, as computed by the hardware.

=h hw_bytes read-only

Returns the number of bytes received by this device, as computed by the hardware.

=h hw_dropped read-only

Returns the number of packets dropped by this device, as computed by the hardware.

=h hw_errors read-only

Returns the number of errors of this device, as computed by the hardware.

=h nombufs read-only

Returns the total number of RX mbuf allocation failures. 

=h rule_add write-only

Inserts a new rule into this device's rule table.
The format of the rule must comply with testpmd. If so, the rule is translated into DPDK's Flow API format.
For example: rule_add ingress pattern eth / ipv4 src is 192.168.100.7 dst is 192.168.1.7 / udp src is 53 / end actions queue index 3 / count / end
A rule could be optionally prepended with the pattern 'flow create X' (where X is the correct port number), otherwise this pattern will be automatically prepended.
Upon success, the ID of the added rule is returned, otherwise an error is returned.

=h rules_del write-only

Deletes a (set of) rule(s) from this device's rule table.
Rule numbers to be deleted are specified as a space-separated list (e.g., rules_del 0 1 2).
Upon success, the number of deleted flow rules is returned, otherwise an error is returned.

=h rules_isolate write-only

Enables/Disables Flow Rule Manager's isolation mode.
Isolated mode guarantees that all ingress traffic comes from defined flow rules only (current and future).
Usage:
    'rules_isolate 0' disables isolation.
    'rules_isolate 1' enables isolation.

=h rules_flush write-only

Deletes all of the rules from this device's rule table.
Upon success, the number of deleted flow rules is returned.

=h rules_ids_global read-only

Returns a string of space-separated global rule IDs that correspond to the rules being installed.

=h rules_ids_internal read-only

Returns a string of space-separated internal rule IDs that correspond to the rules being installed.

=h rules_list read-only

Returns the list of flow rules being installed along with statistics per rule.

=h rules_list_with_hits read-only

Returns the list of flow rules being installed that exhibit at least one hit.
This list is a subset of the list returned by rules_list handler.

=h rules_count read-only

Returns the number of flow rules being installed.

=h

=a DPDKInfo, ToDPDKDevice */

class ToDPDKDevice;

class FromDPDKDevice : public RXQueueDevice {
public:

    FromDPDKDevice() CLICK_COLD;
    ~FromDPDKDevice() CLICK_COLD;

    const char *class_name() const override { return "FromDPDKDevice"; }
    const char *port_count() const override { return PORTS_0_1; }
    const char *processing() const override { return PUSH; }
    void* cast(const char* name) override;

    int configure_phase() const override {
        return CONFIGURE_PHASE_PRIVILEGED - 5;
    }
    bool can_live_reconfigure() const override { return false; }

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
    int initialize(ErrorHandler *) override CLICK_COLD;
    void add_handlers() override CLICK_COLD;
    void cleanup(CleanupStage) override CLICK_COLD;
    bool run_task(Task *) override;
#if HAVE_DPDK_INTERRUPT
    void selected(int fd, int mask) override;
#endif

    void clear_buffers() CLICK_COLD;
    inline DPDKDevice *get_device() {
        return _dev;
    }

#if HAVE_DPDK_READ_CLOCK
    static uint64_t read_clock(void* thunk);
#endif

    inline EthernetDevice *get_eth_device() {
        return _dev->get_eth_device();
    }

protected:
    static int reset_load_handler(
        const String &, Element *, void *, ErrorHandler *
    ) CLICK_COLD;
    static String read_handler(Element *, void *) CLICK_COLD;
    static int write_handler(
        const String &, Element *, void *, ErrorHandler *
    ) CLICK_COLD;
#if RTE_VERSION >= RTE_VERSION_NUM(20,2,0,0)
    static int flow_handler (
        const String &, Element *, void *, ErrorHandler *
    ) CLICK_COLD;
#endif
    static String status_handler(Element *e, void *thunk) CLICK_COLD;
    static String statistics_handler(Element *e, void *thunk) CLICK_COLD;
    static int xstats_handler(int operation, String &input, Element *e,
                              const Handler *handler, ErrorHandler *errh);

    DPDKDevice* _dev;
#if HAVE_DPDK_INTERRUPT
    int _rx_intr;
    class FDState { public:
        FDState() : mustresched(0) {};
        int mustresched;
    };
    per_thread<FDState> _fdstate;
#endif
    bool _set_timestamp;
    bool _tco;
    bool _uco;
    bool _ipco;
    bool _clear;
};

CLICK_ENDDECLS

#endif // CLICK_FROMDPDKDEVICE_HH
