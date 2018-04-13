#ifndef CLICK_BLACKBOXNF_USERLEVEL_HH
#define CLICK_BLACKBOXNF_USERLEVEL_HH

#include <click/task.hh>
#include <click/notifier.hh>
#include <click/batchelement.hh>
#include <click/dpdkdevice.hh>

CLICK_DECLS

/*
=title BlackboxNF

=c

BlackboxNF(MEMPOOL [, I<keywords> BURST, NDESC])

=s netdevices

*/

class BlackboxNF : public BatchElement {

    public:
        BlackboxNF () CLICK_COLD;
        ~BlackboxNF() CLICK_COLD;

        const char    *class_name() const { return "BlackboxNF"; }
        const char    *port_count() const { return PORTS_1_1X2; }
        const char    *processing() const { return PUSH; }
        const char    *flow() const { return "x/y"; }
        int       configure_phase() const { return CONFIGURE_PHASE_PRIVILEGED + 5; }
        bool can_live_reconfigure() const { return false; }

        int  configure   (Vector<String> &, ErrorHandler *) CLICK_COLD;
        int  initialize  (ErrorHandler *)           CLICK_COLD;
        void add_handlers()                 CLICK_COLD;
        void cleanup     (CleanupStage)             CLICK_COLD;

        // Calls either push_packet or push_batch
        bool run_task(Task *);
        void run_timer(Timer *);
        void set_flush_timer(DPDKDevice::TXInternalQueue &iqueue);
        void runSlave();

    private:
        Task _task;

        struct rte_mempool *_message_pool;
        struct rte_ring    *_recv_ring;
        struct rte_ring    *_recv_ring_reverse;
        struct rte_ring    *_send_ring;
        DPDKDevice::TXInternalQueue _iqueue;

        String _MEM_POOL;
        String _PROC_1;
        String _PROC_REVERSE;
        String _PROC_2;

        String _exec;
        String _args;
        unsigned     _ndesc;
        unsigned     _burst_size;
        unsigned int _iqueue_size;
        short        _numa_zone;
        bool _manual;

        unsigned int _internal_tx_queue_size;

        short        _timeout;
        bool         _blocking;
        bool         _congestion_warning_printed;

        counter_t    _n_sent;
        counter_t    _n_dropped;
	int _flags;

        static String read_handler(Element*, void*) CLICK_COLD;
        void push_batch(int, PacketBatch *head);
        void flush_internal_tx_ring(DPDKDevice::TXInternalQueue &iqueue);
};

CLICK_ENDDECLS

#endif // CLICK_BLACKBOXNF_USERLEVEL_HH
