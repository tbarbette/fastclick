#ifndef CLICK_FROMDPDKRING_USERLEVEL_HH
#define CLICK_FROMDPDKRING_USERLEVEL_HH

#include <click/task.hh>
#include <click/notifier.hh>
#include <click/batchelement.hh>
#include <click/dpdkdevice.hh>

CLICK_DECLS

/*
=title FromDPDKRing

=c

FromDPDKRing(MEMPOOL [, I<keywords> BURST, NDESC])

=s netdevices

reads packets from a circular ring buffer using DPDK (user-level)

=d

Reads packets from the ring buffer with name MEMPOOL.
On the contrary to FromDevice.u which acts as a sniffer by default, packets
received by devices put in DPDK mode will NOT be received by the kernel, and
will thus be processed only once.

Arguments:

=over 8

=item MEMPOOL

String. The name of the memory pool to attach.

=item FROM_PROC

String. The name of the Click-DPDK process that uses this element.
It can be a made-up name, however, the other end (a ToDPDKRing)
must use the same name at its own TO_PROC parameter.

=item TO_PROC

String. The name of the Click-DPDK process that is attached to our process.
It can be a made-up name, however, the other end (a ToDPDKRing)
must use the same name at its own FROM_PROC parameter.

=item BURST

Integer. Maximum number of packets that will be processed before rescheduling.
The default is 32.

=item NDESC

Integer. Number of descriptors per ring. The default is 1024.

=item NUMA_ZONE

Integer. The NUMA memory zone (or CPU socket ID) where we allocate resources.

=back

This element is only available at user level, when compiled with DPDK support.

=e

  FromDPDKRing(MEM_POOL 1, FROM_PROC nf1_rx, TO_PROC nf2_tx) -> ...

=h pkt_count read-only

Returns the number of packets read from the ring.

=h byte_count read-only

Returns the number of bytes read from the ring.

=a DPDKInfo, ToDPDKRing */

class FromDPDKRing : public BatchElement, DPDKRing {

    public:
        FromDPDKRing () CLICK_COLD;
        ~FromDPDKRing() CLICK_COLD;

        const char    *class_name() const { return "FromDPDKRing"; }
        const char    *port_count() const { return PORTS_0_1; }
        const char    *processing() const { return PUSH; }
        int       configure_phase() const { return CONFIGURE_PHASE_PRIVILEGED + 1; }
        bool can_live_reconfigure() const { return false; }

        int  configure   (Vector<String> &, ErrorHandler *) CLICK_COLD;
        int  initialize  (ErrorHandler *)           CLICK_COLD;
        void add_handlers()                 CLICK_COLD;
        void cleanup     (CleanupStage)             CLICK_COLD;

        // Calls either push_packet or push_batch
        bool run_task    (Task *);

    private:
        Task _task;

        unsigned int _iqueue_size;

        static String read_handler(Element*, void*) CLICK_COLD;
};

CLICK_ENDDECLS

#endif // CLICK_FROMDPDKRING_USERLEVEL_HH
