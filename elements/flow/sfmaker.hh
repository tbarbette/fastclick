#ifndef MIDDLEBOX_SFMaker_HH
#define MIDDLEBOX_SFMaker_HH
#include <click/vector.hh>
#include <click/multithread.hh>
#include <click/flow/flowelement.hh>
#include <click/tcphelper.hh>
#include <set>
#include <click/tsctimestamp.hh>

CLICK_DECLS


#if 0
#define sf_assert(...) assert(__VA_ARGS__)
#define sf_debug(...) click_chatter(__VA_ARGS__)
#else
#define sf_assert(...)
#define sf_debug(...)
#endif

// Record advanced statistics
#define SF_ADVANCED_STATS 0

// Use priorities when we select which flow should be sent
#define SF_PRIO 1

// At the beginning, SFMAKER used a pipeline mode. Now we use RTC.
#define SF_PIPELINE 0

// Maintains a LinkedList Data Structure (LLDS) to access ready flows in constant time.
#define SF_LLDS 1

// Create a separate LinkedList for flows with only one packet.
#define SF_LLDS_SP 0

//Directly put the SFSLOT in the FCB
#define SF_SLOT_IN_FCB 1

//Disable SCHED
#define SF_NO_SCHED 0

class SFMaker;

/**
 * The SFSlot is all data remembered per-flow.
 */
class SFSlot { public:
    TSCTimestamp first_seen; //First time this flow was seen
    TSCTimestamp waiting_since; //Time at which first packets were enqueued
    TSCTimestamp last_seen; //Time at which last packets were enqueued
    PacketBatch* batch;
    unsigned burst_sent;
    bool forced_flush;
#if SF_ADVANCED_STATS
    unsigned bursts; //Bursts merged in batch
#endif
    unsigned packet_sent;
#if SF_LLDS
    SFSlot* next;
    SFSlot* prev;

    int fail;

#if SF_LLDS_SP
    SFSlot* sp_next;
    SFSlot* sp_prev;
#endif

    bool inList;

#if SF_LLDS_SP
    bool sp_inList;
#endif

#endif

#if SF_PIPELINE
    SimpleSpinlock lock;
#else
    nolock lock;
#endif

    bool released;

    SFSlot() : lock(), released(0) {
            //Zeroes on allocation of slot
    }

    inline bool active() {
        return first_seen != TSCTimestamp();
    }

    inline bool empty() {
        return batch == 0;
    }

    inline int prio (const TSCTimestamp &now, const SFMaker* sf);

       

    inline PacketBatch* dequeue() {
        PacketBatch* q = batch;
        batch = 0;
        return q;
    }

    inline void enqueue(PacketBatch* q) {
        sf_assert(q->tail()->next() == 0);
        if (batch)
            batch->append_batch(q);
        else
            batch = q;

        sf_assert(batch->tail()->next() == 0);
    }

    inline bool ready(TSCTimestamp now, SFMaker* maker);

    inline TSCTimestamp expiry(SFMaker* maker);
#if SF_LLDS
    inline TSCTimestamp sp_expiry(SFMaker* maker);
#endif
    inline void reset(TSCTimestamp now) {
        first_seen = now;
        waiting_since = TSCTimestamp();
                last_seen = TSCTimestamp();
        batch = 0;
        fail = 0;
        burst_sent = 0;
        forced_flush = false;
#if SF_ADVANCED_STATS
        bursts = 0;
#endif
        packet_sent = 0;
#if SF_LLDS
        next = 0;
        prev = 0;
        inList = false;

# if SF_LLDS_SP
        sp_next = 0;
        sp_prev = 0;
        sp_inList = false;
# endif
#endif
    }
};

#if SF_SLOT_IN_FCB
typedef SFSlot SFFlow;
#else
struct SFFlow {
    SFFlow() : index(0) {
    }
    int index;

};
#endif


/*
=c

SFMaker()

=s flow

Delay packets up to DELAY in the hope that bursts can be merged. Then sends packets by (eventually) merged bursts, reordered by flow priority.

=d

Buffer packets up to DELAY in the hope of receiving multiple packets of the same flow.
Eventually, sends packets by merged bursts, reordered by flow priority.
The goal is increasing packets spatial locality to acheive higher performance in next steps/NFs. 


Arguments:

=item DELAY

Packets buffering time in micro-seconds.

=item PROTO_COMPRESS

Enables TCP protocol compressor! Default is 0.

=item REORDER

Reorder TCP packets of the same flow if they are not arrived in order! Default is 1.

=item BYPASS_SYN

Bypass SYN packets since the possibility of receiving another packet of the same flow within the buffering time is almost zero! Default is 0.

=item MAX_TX_BURST

The maximum burst size of output. Default is 32.

=item MAX_CAP

Maximum number of packets that Reframer can hold at the same time. If number of saved packets reach the maximum capacity, reframer bypasses incoming packets. Default is -1, which means there is no limitation.
*/

enum Prio {
    PRIO_FIRST_SEEN,
    PRIO_SENT,
    PRIO_DELAY
};

enum Model {
    MODEL_NONE,
    MODEL_SECOND,
};

struct Burst {
    int prio;
    PacketBatch* batch;
    FlowControlBlock* fcb;
};

class SFMaker : public FlowStateElement<SFMaker,SFFlow>, public Router::InitFuture, protected TCPHelper
{

public:
    /** @brief Construct an SFMaker element
     */
    SFMaker() CLICK_COLD;

    const char *class_name() const        { return "SFMaker"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
    int solve_initialize(ErrorHandler *errh) override CLICK_COLD;
    
    bool get_spawning_threads(Bitvector& b, bool isoutput, int port) override;

    void release_flow(SFFlow* fcb);

    void run_timer(Timer* t) override;

    typedef std::multiset<Burst> FlowQueue;

    inline void prepareBurst(SFSlot& f, PacketBatch* &all);
#if SF_PRIO
    inline bool schedule_burst_from_flow(SFSlot& f, FlowQueue& q,  TSCTimestamp& now, unsigned max);
#else
    inline bool schedule_burst_from_flow(SFSlot& f, TSCTimestamp& now, unsigned max);
#endif
    bool run_task(Task* t) override;
    void push_flow(int port, SFFlow* fcb, PacketBatch*);

    inline bool new_flow(SFFlow*, Packet*);
    bool stopClassifier() override CLICK_COLD { return _remanage; };
    static const unsigned timeout = 100000;

    static int write_handler(const String &, Element *, void *, ErrorHandler *) CLICK_COLD;
    static String read_handler(Element *e, void *thunk) CLICK_COLD;
    void add_handlers() override CLICK_COLD;

    inline Prio prio() const {
        return _prio;
    }
protected:

    void handleTCP(PacketBatch* &batch);

    inline int allocate_index();
    inline void release_index(int index);


    Prio _prio;
    Model _model;

    class SFState  { public:

        SFState() :  active(0),
#if !SF_SLOT_IN_FCB
    flows(180000,SFSlot()), indexes(), allocated(), 

        idx_lock(),
#endif
    
    sf(0), sf_size(0), sf_flows(0), pushed(0), sent(0), reordered(0),killed(0), timer(0), task(0)
        {

        }

#if !SF_SLOT_IN_FCB
        Vector<SFSlot> flows;
        Vector<int> indexes;
        Vector<int> allocated;
        #if SF_PIPELINE
        Vector<int> new_allocated;
        #endif
# if SF_PIPELINE
        SimpleSpinlock idx_lock;
# else
        nolock idx_lock;
# endif
#endif
        Timer* timer;
        Task* task;
        
	TSCTimestamp last_tx_time;
	PacketBatch* ready_batch;

#if SF_LLDS
        SFSlot* head_slot;
        SFSlot* tail_slot;
        bool need_schedule;

# if SF_LLDS_SP
        SFSlot* sp_head;
        SFSlot* sp_tail;
# endif
#endif
        //Statistics
        uint32_t active;
        uint64_t sf;
        uint64_t sf_size;
        uint64_t sf_flows;
        uint64_t sent;
        uint64_t pushed;
        uint64_t reordered;
        uint64_t killed;
#if SF_ADVANCED_STATS
        struct Stat {
            unsigned useless_wait;
            unsigned packets;
            unsigned bursts;
            unsigned compressed;
        };
        Vector<Stat> stats;
#endif



    };

    not_per_thread<SFState> _state;

    //Parameters
    int _verbose;
    bool _take_all;
    bool _proto_compress;
    bool _reorder;
    bool _always;
    bool _pass;
    bool _bypass_syn;
    bool _remanage;
    int _bypass_after_fail;
    uint32_t _max_burst;
    uint32_t _max_tx_burst;
    uint32_t _min_tx_burst;
    int64_t _max_tx_delay;
    int _max_capacity;

    TSCTimestamp _delay;
    TSCTimestamp _delay_last;
#if !SF_LLDS
    TSCTimestamp _delay_hard;
#endif
    friend class SFSlot;


#if SF_LLDS_SP
    inline void removeFromSPList(SFFlow &f, SFState &s) {
        s.sp_head = f.sp_next;
        if(s.sp_head == 0)
                s.sp_tail = 0;
        else
                s.sp_tail->prev = 0;
        f.sp_next = 0;
        f.sp_prev = 0;
        f.sp_inList = false;
    }
#endif
    inline void removeFromList(SFFlow &f, SFState &s) {
        s.head_slot = f.next;
        if(s.head_slot == 0)
                s.tail_slot = 0;
        else
                s.head_slot->prev = 0;
        f.next = 0;
        f.prev = 0;
        f.inList = false;
    }

};

        inline TSCTimestamp SFSlot::expiry(SFMaker* maker) {
                   auto d = waiting_since + maker->_delay;
#if SF_LLDS
            return d;
#else
            return min(max(d, last_seen + maker->_delay_last), waiting_since + maker->_delay_hard);
#endif
        }
#if SF_LLDS
                inline TSCTimestamp SFSlot::sp_expiry(SFMaker* maker) {
                       return waiting_since + maker->_delay_last;
        }
#endif
        inline int SFSlot::prio (const TSCTimestamp &now, const SFMaker* sf)
        {
            if (sf->prio() == PRIO_FIRST_SEEN)
                return (now - first_seen).usecval();
            else if (sf->prio() == PRIO_SENT) {
                return -(packet_sent);
            } else //PRIO_DELAY
                return (now - waiting_since).usecval();
        }


CLICK_ENDDECLS
#endif
