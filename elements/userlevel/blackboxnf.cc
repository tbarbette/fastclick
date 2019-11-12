// -*- c-basic-offset: 4; related-file-name: "BlackboxNF.hh" -*-
/*
 * BlackboxNF.{cc,hh} -- element that establishes communication between Click
 * and standalone processes using DPDK ring buffers.
 *
 * Copyright (c) 2018 Tom Barbette, KTH Royal Institute of Technology
 * Copyright (c) 2018 Georgios Katsikas, KTH Royal Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include <click/args.hh>
#include <click/error.hh>
#include <click/standard/scheduleinfo.hh>

#include <unistd.h>

#include "blackboxnf.hh"

CLICK_DECLS

BlackboxNF::BlackboxNF() :
    _task(this), _message_pool(0), _recv_ring(0), _recv_ring_reverse(0), _send_ring(0),
    _iqueue(), _exec(""), _args(""), _ndesc(DPDKDevice::DEF_RING_NDESC), _burst_size(0),
    _numa_zone(0), _manual(false), _internal_tx_queue_size(1024), _timeout(0), _blocking(false),
    _congestion_warning_printed(false), _n_recv(0), _n_sent(0), _n_dropped(0), _flags(0)
{
    in_batch_mode = BATCH_MODE_NEEDED;
}

BlackboxNF::~BlackboxNF()
{
}

int
BlackboxNF::configure(Vector<String> &conf, ErrorHandler *errh)
{
    bool spenq = false;
    bool spdeq = false;

    String _origin = String(click_random() % 65536);
    String _destination = String(click_random() % 65536);

    _PROC_1 = _origin+"_2_"+_destination;
    _PROC_2 = _destination+"_2_"+_origin;
    _PROC_2 = _destination+"_2_"+_origin+"_reverse";


    if (Args(conf, this, errh)
        .read_mp("EXEC", _exec)
        .read_p("ARGS", _args)
        .read("BURST", _burst_size)
        .read("NDESC", _ndesc)
        .read("NUMA_ZONE", _numa_zone)
        .read("POOL", _MEM_POOL)
        .read("MANUAL", _manual)
        .read("TO_RING", _PROC_1)
        .read("TO_REVERSE_RING", _PROC_REVERSE)
        .read("FROM_RING", _PROC_2)
        .read("SP_ENQ", spenq)
        .read("SP_DEQ", spdeq)
        .read("BLOCKING", _blocking)
        .complete() < 0)
        return -1;

    if (spenq) {
        _flags |= RING_F_SP_ENQ;
    }
    if (spdeq) {
        _flags |= RING_F_SC_DEQ;
    }

    if (_MEM_POOL == "") {
        _MEM_POOL = "ring_" + _origin;
    }

    if (_MEM_POOL.empty() || (_MEM_POOL.length() == 0)) {
        return errh->error("[%s] Enter MEM_POOL name", name().c_str());
    } else {
        _MEM_POOL = DPDKDevice::MEMPOOL_PREFIX + _MEM_POOL;
        click_chatter("Mempool name is %s", _MEM_POOL.c_str());
    }

    if (_ndesc == 0) {
        _ndesc = DPDKDevice::DEF_RING_NDESC;
        click_chatter("[%s] Default number of descriptors is set (%d)\n", name().c_str(), _ndesc);
    }

    // If zone number not given, assume that this process belongs to the memory zone of device 0
    if (_numa_zone < 0) {
        click_chatter("[%s] Assuming NUMA zone 0\n", name().c_str());
        _numa_zone = 0;
    }

    return 0;
}

int
BlackboxNF::initialize(ErrorHandler *errh)
{
    if (_burst_size == 0) {
        _burst_size = DPDKDevice::DEF_BURST_SIZE;
        errh->warning("[%s] Non-positive BURST number. Setting default (%d)\n", name().c_str(), _burst_size);
    }

    if ((_ndesc > 0) && ((unsigned)_burst_size > _ndesc / 2)) {
        errh->warning("[%s] BURST should not be greater than half the number of descriptors (%d)\n", name().c_str(), _ndesc);
    } else if (_burst_size > DPDKDevice::DEF_BURST_SIZE) {
        errh->warning("[%s] BURST should not be greater than %d as DPDK won't send more packets at once\n", name().c_str(), DPDKDevice::DEF_BURST_SIZE);
    }

    _recv_ring = rte_ring_create(_PROC_1.c_str(), DPDKDevice::RING_SIZE, rte_socket_id(), _flags);

    if (_PROC_REVERSE) {
        _recv_ring_reverse = rte_ring_create(_PROC_REVERSE.c_str(), DPDKDevice::RING_SIZE, rte_socket_id(), _flags);
    }

    _send_ring = rte_ring_create(_PROC_2.c_str(), DPDKDevice::RING_SIZE, rte_socket_id(), _flags);

    _message_pool = rte_mempool_lookup(_MEM_POOL.c_str());
    if (!_message_pool) {
        _message_pool = rte_mempool_create(
            _MEM_POOL.c_str(), _ndesc,
            DPDKDevice::MBUF_DATA_SIZE,
            DPDKDevice::RING_POOL_CACHE_SIZE,
            DPDKDevice::RING_PRIV_DATA_SIZE,
            NULL, NULL, NULL, NULL,
            rte_socket_id(), _flags
        );
    }

    // Initialize the internal queue
    _iqueue.pkts = new struct rte_mbuf *[_internal_tx_queue_size];
    if (_timeout >= 0) {
        _iqueue.timeout.assign(this);
        _iqueue.timeout.initialize(this);
        _iqueue.timeout.move_thread(click_current_cpu_id());
    }

    if (!_recv_ring) {
        return errh->error("[%s] Problem getting Rx ring. "
                    "Make sure that the involved processes have a correct ring configuration\n", name().c_str());
    }

    if (!_send_ring) {
        return errh->error("[%s] Problem getting Tx ring. "
                    "Make sure that the involved processes have a correct ring configuration\n", name().c_str());
    }

    if (!_message_pool) {
        return errh->error("[%s] Problem getting message pool %s. "
                    "Make sure that the involved processes have a correct ring configuration\n", name().c_str(), _MEM_POOL.c_str());
    }

    // Schedule the element
    ScheduleInfo::initialize_task(this, &_task, true, errh);

    Bitvector cpu;
    cpu.resize(click_max_cpu_ids());
    cpu[home_thread_id()] = true;
    int pid = run_slave(_exec, _args, _manual, cpu, _MEM_POOL);

    return 0;
}

void
BlackboxNF::cleanup(CleanupStage)
{
    if (_iqueue.pkts) {
        delete[] _iqueue.pkts;
    }
}

int
BlackboxNF::run_slave(String exec, String args, bool manual, Bitvector cpus, String pool)
{
    click_chatter("Launching slave!");

    Vector<char*> chars;
    chars.push_back(const_cast<char*>(exec.c_str()));
    click_chatter("Building cmdline");
    char buff[1024];
    char* a = buff;
    strcpy(a,args.c_str());
    char* begin = a;
    bool quote = false;
    String tmp;
    while (*a != 0) {
        if (!quote && *a == ' ') {
            *a = '\0';

            char* cpy = (char*)malloc((strlen(begin) + 1) * sizeof(char));
            strcpy(cpy, begin);
            chars.push_back(cpy);
            click_chatter("%s",begin);
            begin = a + 1;
        }
        if (*a == '$') {
            if (strncmp(a + 1, "POOL", 4) == 0) {
                if (!pool) {
                    click_chatter("$POOL variable used but this method does not use memory pools!");
                    return -1;
                }
                int cur = a-begin;
                *a = '\0';
                tmp = String(begin) + pool + String(a+5);
                begin = const_cast<char*>(tmp.c_str());
                a = begin + cur + pool.length() - 1;
            } else if (strncmp(a + 1, "CPU_RANGE", 9) == 0) {
                int cur = a-begin;
                *a = '\0';
                String s = cpus.unparse();
                tmp = String(begin) + s + String(a + 10);
                begin = const_cast<char*>(tmp.c_str());
                a = begin + cur + s.length() - 1;
            }

        }
        if (*a == '"') {
            quote = !quote;
            memmove(a, a+1, strlen(a));
        } else {
            a++;
        }
    }

    if (begin != a) {
        chars.push_back(begin);
    }
    chars.push_back(0);

    if (manual) {
        String s ="";
        for (int i = 0; i < chars.size(); i++) {
            s = s + chars[i] + " ";
        }
        click_chatter("Command line : %s", s.c_str());
    }

    // Launch slave
    int pid = fork();
    if (pid == -1) {
        click_chatter("Fork error. Too many processes?");
        return -1;
    }

    if (pid == 0) {
        int ret;
        if ((ret = execvp(chars[0], chars.data()))) {
            click_chatter("Could not launch slave process: %d %d", ret, errno);
        }
        exit(1);
    } else {
        return pid;
    }
}

bool
BlackboxNF::run_task(Task *t)
{
    PacketBatch    *head = NULL;
    WritablePacket *last = NULL;

    struct rte_mbuf *pkts[_burst_size];

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
    int n = rte_ring_dequeue_burst(_recv_ring, (void **)pkts, _burst_size, 0);
#else
    int n = rte_ring_dequeue_burst(_recv_ring, (void **)pkts, _burst_size);
#endif
    if (n < 0) {
        click_chatter("[%s] Couldn't read from the Rx rings\n", name().c_str());
        return false;
    }

    // Turn the received frames into Click frames
    for (unsigned i = 0; i < n; ++i) {
        rte_prefetch0(rte_pktmbuf_mtod(pkts[i], void *));
        WritablePacket *p = Packet::make(
            rte_pktmbuf_mtod(pkts[i], unsigned char *),
            rte_pktmbuf_data_len(pkts[i]),
            DPDKDevice::free_pkt,
            pkts[i],
            rte_pktmbuf_headroom(pkts[i]),
            rte_pktmbuf_tailroom(pkts[i])
        );

        p->set_packet_type_anno(Packet::HOST);

        if (head == NULL)
            head = PacketBatch::start_head(p);
        else
            last->set_next(p);
        last = p;
    }

    if (head) {
        head->make_tail  (last, n);
        output_push_batch(0, head);
        _n_recv += n;
    }

    _task.fast_reschedule();

    return true;
}

void
BlackboxNF::run_timer(Timer *)
{
    flush_internal_tx_ring(_iqueue);
}

inline void
BlackboxNF::set_flush_timer(DPDKDevice::TXInternalQueue &iqueue)
{
    if (_timeout >= 0) {
        if (iqueue.timeout.scheduled()) {
            // No more pending packets, remove timer
            if ( iqueue.nr_pending == 0 )
                iqueue.timeout.unschedule();
        }
        else {
            if (iqueue.nr_pending > 0) {
                // Pending packets, set timeout to flush packets after a while even without burst
                if (_timeout == 0) {
                    iqueue.timeout.schedule_now();
                } else {
                    iqueue.timeout.schedule_after_msec(_timeout);
                }
            }
        }
    }
}

/**
 * Flush as many packets as possible from the internal queue of the DPDK ring.
 */
void
BlackboxNF::flush_internal_tx_ring(DPDKDevice::TXInternalQueue &iqueue)
{
    unsigned n;
    unsigned sent = 0;
    //
    // sub_burst is the number of packets DPDK should send in one call
    // if there is no congestion, normally 32. If it sends less, it means
    // there is no more room in the output ring and we'll need to come
    // back later. Also, if we're wrapping around the ring, sub_burst
    // will be used to split the burst in two, as rte_ring_enqueue_burst
    // needs a contiguous buffer space.
    //
    unsigned sub_burst;

    do {
        sub_burst = iqueue.nr_pending > DPDKDevice::DEF_BURST_SIZE ?
            DPDKDevice::DEF_BURST_SIZE : iqueue.nr_pending;

        // The sub_burst wraps around the ring
        if (iqueue.index + sub_burst >= _internal_tx_queue_size)
            sub_burst = _internal_tx_queue_size - iqueue.index;

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
        n = rte_ring_enqueue_burst(
            _send_ring, (void* const*)(&iqueue.pkts[iqueue.index]),
            sub_burst,
            0
        );
#else
        n = rte_ring_enqueue_burst(
            _send_ring, (void* const*)(&iqueue.pkts[iqueue.index]),
            sub_burst
        );
#endif
        iqueue.nr_pending -= n;
        iqueue.index      += n;

        // Wrapping around the ring
        if (iqueue.index >= _internal_tx_queue_size)
            iqueue.index = 0;

        sent += n;
    } while ((n == sub_burst) && (iqueue.nr_pending > 0));

    _n_sent += sent;

    // If ring is empty, reset the index to avoid wrap ups
    if (iqueue.nr_pending == 0) {
        iqueue.index = 0;
    }
}


void
BlackboxNF::push_batch(int, PacketBatch *head)
{
    // Get the internal queue
    DPDKDevice::TXInternalQueue &iqueue = _iqueue;

    Packet *p    = head;
    Packet *next = NULL;

    // No recycling through Click if we have DPDK packets
    bool congestioned;
#if !CLICK_PACKET_USE_DPDK
    BATCH_RECYCLE_START();
#endif

    do {
        congestioned = false;

        // First, place the packets in the queue, while there is still place there
        while (iqueue.nr_pending < _internal_tx_queue_size && p) {
            struct rte_mbuf *mbuf = DPDKDevice::get_mbuf(p, true, _numa_zone);
            if (mbuf != NULL) {
                iqueue.pkts[(iqueue.index + iqueue.nr_pending) % _internal_tx_queue_size] = mbuf;
                iqueue.nr_pending++;
            }
            next = p->next();

        #if !CLICK_PACKET_USE_DPDK
            BATCH_RECYCLE_UNKNOWN_PACKET(p);
        #endif

            p = next;
        }

        // There are packets not pushed into the queue, congestion is very likely!
        if (p != 0) {
            congestioned = true;
            if ( !_congestion_warning_printed ) {
                if ( !_blocking )
                    click_chatter("[%s] Packet dropped", name().c_str());
                else
                    click_chatter("[%s] Congestion warning", name().c_str());
                _congestion_warning_printed = true;
            }
        }

        // Flush the queue if we have pending packets
        if ((int) iqueue.nr_pending > 0) {
            flush_internal_tx_ring(iqueue);
        }
        set_flush_timer(iqueue);

        // If we're in blocking mode, we loop until we can put p in the iqueue
    } while (unlikely(_blocking && congestioned));

#if !CLICK_PACKET_USE_DPDK
    // If non-blocking, drop all packets that could not be sent
    while (p) {
        next = p->next();
        BATCH_RECYCLE_UNKNOWN_PACKET(p);
        p = next;
        ++_n_dropped;
    }
#endif

#if !CLICK_PACKET_USE_DPDK
    BATCH_RECYCLE_END();
#endif

}

String
BlackboxNF::read_handler(Element *e, void *user_data)
{
    BlackboxNF *fr = static_cast<BlackboxNF*>(e);
    if (!fr) {
        return "Blackbox instance unavailable";
    }

    intptr_t what = reinterpret_cast<intptr_t>(user_data);
    switch (what) {
        case h_rx_count:
            return String(fr->_n_recv);
        case h_tx_count:
            return String(fr->_n_sent);
        case h_drop:
            return String(fr->_n_dropped);
        default:
            return "<undefined>";
    }
}

void
BlackboxNF::add_handlers()
{
    add_read_handler("recv", read_handler, h_rx_count);
    add_read_handler("sent", read_handler, h_tx_count);
    add_read_handler("drop", read_handler, h_drop);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel dpdk batch)
EXPORT_ELEMENT(BlackboxNF)
ELEMENT_MT_SAFE(BlackboxNF)
