#ifndef CLICK_TODPDKDEVICE_USERLEVEL_HH
#define CLICK_TODPDKDEVICE_USERLEVEL_HH

#include <click/batchelement.hh>
#include <click/sync.hh>
#include <click/dpdkdevice.hh>
#include "kernelfilter.hh"
#include "queuedevice.hh"
CLICK_DECLS

/*
 * =title ToDpdkDevice
 *
 * =c
 *
 * ToDpdkDevice(DEVNAME)
 *
 * =s netdevices
 *
 * sends packets to network device using Intel's DPDK (user-level)
 *
 * =d
 *
 * Sends pushed packets to the network device named DEVNAME, using Intel's
 * DPDK.
 *
 * =back
 *
 * This element is only available at user level, when compiled with DPDK
 * support using the --enable-dpdk switch.
 *
 * =e
 *
 *   ... -> ToDpdkDevice(1)
 *
 * =item DEVNAME
 *
 * String Device name
 *
 * =item IQUEUE (push mode only)
 *
 * Unsigned integer Number of packets that we can bufferize if all output rings are full while in push mode
 *
 * =item BLOCKANT (push mode only)
 *
 * Boolean. If true and packets are pushed and the IQUEUE is full, we'll block until there is space in the output ring, or we'll drop. Default true.
 *
 * =item BURST
 *
 * Number of packets to batch before sending them out.
 *
 * =item MINTHREADS
 *
 * Minimum number of threads to use.
 *
 * =item MAXTHREADS
 *
 * Maximum number of threads to use.
 *
 * =item MAXQUEUES
 *
 * Maximum number of queues to use. Normally, it will use as many queues as the number of threads which can end up in this element. If the limit is lower than the number of threads, locking will be used before accessing the queues which will be shared between multiple threads.
 *
 * =n
 *
 * =h n_sent read-only
 *
 * Returns the number of packets sent by the device.
 *
 * =h n_dropped read-only
 *
 * Returns the number of packets dropped by the device.
 *
 * =h reset_counts write-only
 *
 * Resets n_send and n_dropped counts to zero.
 *
 * =a
 * FromDpdkDevice
 */

class ToDpdkDevice : public QueueDevice {
public:

    ToDpdkDevice() CLICK_COLD;
    ~ToDpdkDevice() CLICK_COLD;

    const char *class_name() const      { return "ToDpdkDevice"; }
    const char *port_count() const      { return PORTS_1_0; }
    const char *processing() const      { return PUSH; }
    int configure_phase() const {
        return KernelFilter::CONFIGURE_PHASE_TODEVICE;
    }
    bool can_live_reconfigure() const   { return false; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;

    void cleanup(CleanupStage stage) CLICK_COLD;

    void add_handlers() CLICK_COLD;

#if HAVE_BATCH
    void push_batch(int port, PacketBatch *head);
#endif
    void push(int port, Packet *p);

private:

    static String read_handler(Element*, void*) CLICK_COLD;
    static int write_handler(const String&, Element*, void*, ErrorHandler*)
        CLICK_COLD;


    class State {
    public:
    	State() : glob_pkts(NULL), _int_index(0), _int_left(0) {

    	}
    	struct rte_mbuf ** glob_pkts;
    	unsigned int _int_index;
		unsigned int _int_left;
    };

    per_thread<State> state;
    unsigned _port_no;
    unsigned int _internal_queue;
    unsigned int _burst;
};

CLICK_ENDDECLS

#endif // CLICK_TODPDKDEVICE_USERLEVEL_HH
