#ifndef CLICK_TODPDKRING_USERLEVEL_HH
#define CLICK_TODPDKRING_USERLEVEL_HH

#include <click/batchelement.hh>
#include <click/sync.hh>
#include <click/dpdkdevice.hh>

CLICK_DECLS

/*
=title ToDPDKRing

=c

ToDPDKRing(MEMPOOL m, FROM_PROC p1, TO_PROC p2, [, I<keywords> BURST, etc.])

=s netdevices

sends packets to a circular ring buffer using DPDK (user-level).

=d

Sends packets to the ring buffer with name MEMPOOL. As DPDK does
not support polling, this element only supports PUSH. It will build a batch of
packets inside an internal queue (limited to IQUEUE packets) until it reaches
BURST packets, and then send the batch to DPDK. If the batch is not ready after
TIMEOUT ms, it will flush the batch of packets even if it doesn't contain
BURST packets.

Arguments:

=over 8

=item MEMPOOL

String. The name of the memory pool to attach.

=item FROM_PROC

String. The name of the Click-DPDK process that uses this element.
It can be a made-up name, however, the other end (a FromDPDKRing)
must use the same name at its own TO_PROC parameter.

=item TO_PROC

String. The name of the Click-DPDK process that is attached to our process.
It can be a made-up name, however, the other end (a FromDPDKRing)
must use the same name at its own FROM_PROC parameter.

=item IQUEUE

Integer.  Size of the internal queue, i.e. number of packets that we can buffer
before pushing them to the DPDK framework. If IQUEUE is bigger than BURST,
some packets could be buffered in the internal queue when the output ring is
full. Defaults to 1024.

=item BLOCKING

Boolean.  If true, when there is no more space in the output device ring, and
the IQUEUE is full, we'll block until some packet could be sent. If false the
packet will be dropped. Defaults to true.

=item BURST

Integer. Number of packets to batch before sending them out. A bigger BURST
leads to more latency, but a better throughput. The default value of 32 is
recommended as it is what DPDK will do under the hood. Prefer to set the
TIMEOUT parameter to 0 if the throughput is low as it will maintain
performance.

=item TIMEOUT

Integer.  Set a timeout to flush the internal queue. It is useful under low
throughput as it could take a long time before reaching BURST packet in the
internal queue. The timeout is expressed in milliseconds. Setting the timer to
0 is not a bad idea as it will schedule after the source element (such as a
FromDPDKDevice) will have finished its burst, or all incoming packets. This
would therefore ensure that a flush is done right after all packets have been
processed by the Click pipeline. Setting a negative value disable the timer,
this is generally acceptable if the thoughput of this element rarely drops
below 32000 pps (~50 Mbps with maximal size packets) with a BURST of 32, as the
internal queue will wait on average 1 ms before containing 32 packets. Defaults
to 0 (immediate flush).

=item NDESC

Integer. Number of descriptors per ring. The default is 1024.

=item NUMA_ZONE

Integer. The NUMA memory zone (or CPU socket ID) where we allocate resources.

=back

This element is only available at user level, when compiled with DPDK support.

=e

  ... -> ToDPDKRing(MEM_POOL 2, FROM_PROC nf1_tx, TO_PROC nf2_rx, IQUEUE 1024, BURST 32)

=h count read-only

Returns the number of packets sent by the device.

=h dropped read-only

Returns the number of packets dropped by the device.

=a DPDKInfo, FromDPDKRing */

class ToDPDKRing : public BatchElement, DPDKRing {

    public:
        ToDPDKRing () CLICK_COLD;
        ~ToDPDKRing() CLICK_COLD;

        const char    *class_name() const { return "ToDPDKRing"; }
        const char    *port_count() const { return PORTS_1_0; }
        const char    *processing() const { return PUSH; }
        int       configure_phase() const { return CONFIGURE_PHASE_PRIVILEGED + 2; }
        bool can_live_reconfigure() const { return false; }

        int  configure   (Vector<String> &, ErrorHandler *)     CLICK_COLD;
        int  initialize  (ErrorHandler *)           CLICK_COLD;
        void cleanup     (CleanupStage stage)           CLICK_COLD;
        void add_handlers()                     CLICK_COLD;

        void run_timer(Timer *);

    #if HAVE_BATCH
        void push_batch (int port, PacketBatch *head);
    #endif
        void push(int port, Packet      *p);

    private:

        inline void set_flush_timer(DPDKDevice::TXInternalQueue &iqueue);
        void flush_internal_tx_ring(DPDKDevice::TXInternalQueue &iqueue);

        DPDKDevice::TXInternalQueue     _iqueue;

        unsigned int _internal_tx_queue_size;

        short        _timeout;
        bool         _blocking;
        bool         _congestion_warning_printed;

        counter_t _dropped;

        static String read_handler(Element*, void*) CLICK_COLD;
};

CLICK_ENDDECLS

#endif // CLICK_TODPDKRING_USERLEVEL_HH
