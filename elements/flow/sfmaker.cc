/*
 * SFMaker.{cc,hh} -- Delay packets for a given time.
 * 
 * Copyright (c) 2021 Tom Barbette
 * Copyright (c) 2021 Hamid Ghasemi
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
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/ether.h>
#include "sfmaker.hh"
#include "../flow/tcpreorder.hh"

CLICK_DECLS

inline bool
SFSlot::ready(TSCTimestamp now, SFMaker* maker) {
    if (waiting_since == TSCTimestamp()){
        return false;
    }
    if (empty()){
        return false;
    }
    if (batch->count() > maker->_max_burst) {
        return true;
    }

    if (forced_flush)
    return true;

    if (maker->_model == MODEL_SECOND) {
        if (packet_sent < 1)
            return true;
    }

    if (now >= expiry(maker)) {
        return true;
    }
    return false;
}


#if !SF_SLOT_IN_FCB
int SFMaker::allocate_index() {
#if SF_LLDS && !SF_SLOT_IN_FCB
    //Release candidate SFSlots if there is no more space to allocate.
    if(_state->indexes.size() <= 0){
        auto &s = *_state;
        int n = s.allocated.size();
        for (int j = 0; j < n; j++) {
          redo:
            int i = s.allocated[j];
            SFSlot &f = s.flows[i];
            if (unlikely(f.released)) {
                        f.lock.acquire();
                f.released = 0;
                f.lock.release();
                s.idx_lock.acquire();
                s.indexes.push_back(i);
                s.idx_lock.release();
                --n;
                if (j < n) {
                            s.allocated[j] = s.allocated[n];
                goto redo;
                        } else {
                break;
                }
            }
        }
    }
#endif
        sf_assert(_state->indexes.size() > 0);
        _state->idx_lock.acquire();
        int index = _state->indexes.back();
        _state->indexes.pop_back();
        _state->idx_lock.release();
        return index;
    }
#endif

SFMaker::SFMaker() : _verbose(1), _take_all(1), _state()
{

}

int SFMaker::configure(Vector<String> &conf, ErrorHandler *errh)
{
    int delay, delay_last, delay_hard;
    String prio, model;
    bool pass;

    VirtualFlowManager::fcb_builded_init_future()->post(this);

    if(Args(conf, this, errh)
            .read_or_set_p("DELAY", delay, 100)
            .read_or_set_p("DELAY_LAST", delay_last, 0)
            .read_or_set_p("DELAY_HARD", delay_hard, 0)
            .read_or_set("VERBOSE", _verbose, 0)
            .read_or_set("TAKE_ALL", _take_all, false)
            .read_or_set("PROTO_COMPRESS", _proto_compress, false)
            .read_or_set("REORDER", _reorder, true)
            .read_or_set("PRIO", prio, "SENT")
            .read_or_set("PASSTHROUGH", pass, true)
            .read_or_set("MODEL", model, "SECOND")
            .read_or_set("BYPASS_SYN", _bypass_syn, false)
            .read_or_set("BYPASS_AFTER_FAIL", _bypass_after_fail, 0)
            .read_or_set("MAX_BURST", _max_burst, 1024)
            .read_or_set("MAX_TX_BURST", _max_tx_burst, 32)
            .read_or_set("MIN_TX_BURST", _min_tx_burst, 1)
            .read_or_set("MAX_TX_DELAY", _max_tx_delay, 0)
            .read_or_set("ALWAYSUP", _always, false)
            .read_or_set("REMANAGE", _remanage, false)
            .read_or_set("MAX_CAP", _max_capacity, -1)
            .complete() < 0)
        return -1;

    if (prio == "SENT") {
        _prio = PRIO_SENT;
    } else if (prio == "DELAY") {
        _prio = PRIO_DELAY;
    } else if (prio == "FIRST") {
        _prio = PRIO_FIRST_SEEN;
    } else {
        return errh->error("Unknown PRIO !");
    }

    if (model == "SECOND") {
        _model = MODEL_SECOND;
    } else if (model == "NONE") {
        _model = MODEL_NONE;
    } else {
        errh->error("Unknown MODEL !");
    }

    _pass = pass;

    _delay = TSCTimestamp::make_usec(delay);

    _delay_last = TSCTimestamp::make_usec(delay_last);
#if SF_LLDS

#if !SF_LLDS_SP
    if (delay_last != 0)
        return errh->error("DELAY_LAST must be 0 if SF_LLDS is enabled and not SF_LLDS_SP");
#endif
    click_chatter("Delay is %lu cycles, last %lu", _delay.tsc_val(), _delay_last.tsc_val());
    if (delay_hard != 0) {
        return errh->error("SFDHARD is not supported with LLDS. It must be 0.");
    }
#else
    _delay_hard = TSCTimestamp::make_usec(delay_hard);
    click_chatter("Delay is %lu cycles, last %lu and hard %lu", _delay.tsc_val(), _delay_last.tsc_val(), _delay_hard.tsc_val());
#endif
    return 0;
}

int SFMaker::solve_initialize(ErrorHandler *errh)
{
    auto t = get_pushing_threads();
    if (t.weight() > 1)
        return errh->error("Element is not MT safe and is traversed by %s", t.unparse().c_str());
    if (t.weight() == 0) {
        errh->warning("Element is not traversed by any thread !?");
        return 0;
    }


    int tid = t.clz();

    click_chatter("SFMaker %s will use thread %d",t.unparse().c_str(), tid);

    for (int i = 0; i < _state.weight(); i ++) {
        auto &s = _state.get_value(i);
        s.timer = new Timer(this);
        s.task = new Task(this);
        if (!_always)
            s.timer->initialize(this);

        s.task->initialize(this, true);
    #if ! SF_PIPELINE
        if (tid != home_thread_id())
            s.task->move_thread(tid);
        click_chatter("%p{element} handled by thread %d in parallel mode", this, tid);
    #endif
#if !SF_SLOT_IN_FCB
        s.indexes.reserve(s.flows.size());
        s.allocated.reserve(s.flows.size());
        for (int i = 0; i < s.flows.size(); i++) {
            s.indexes.push_back(i);
        }
#endif
        s.ready_batch = 0;
    s.last_tx_time = TSCTimestamp::now_steady();

#if SF_LLDS
        s.head_slot = 0;
        s.tail_slot = 0;
        s.need_schedule = true;

# if SF_LLDS_SP
        s.sp_head = 0;
        s.sp_tail = 0;
# endif
#endif
    }
    return Router::InitFuture::solve_initialize(errh);
}

bool operator<(const Burst& lhs, const Burst& rhs) {
    return lhs.prio < rhs.prio;
}

bool
SFMaker::get_spawning_threads(Bitvector& b, bool, int port) {
    #if SF_PIPELINE
        unsigned int thisthread = router()->home_thread_id(this);
        b[thisthread] = 1;
        return false;
    #else
        return true;
    #endif
}

/**
 * Optimize TCP stuffs on the way
 */
void SFMaker::handleTCP(PacketBatch* &batch) {
        Packet* biggest_p = batch->tail();
        tcp_seq_t biggest_ack = ntohl(biggest_p->tcp_header()->th_ack);
        bool hasData = false;
        bool hasSimpleAck = false;
        bool ordered = true;

        tcp_seq_t last_seq = ntohl(batch->first()->tcp_header()->th_seq);

        //TODO SACK
        FOR_EACH_PACKET(batch,p) {
            tcp_seq_t pa = ntohl(p->tcp_header()->th_ack);
            if (SEQ_GT(pa, biggest_ack)) {
                biggest_ack = pa;
                biggest_p = p;
            }

            tcp_seq_t seq = ntohl(p->tcp_header()->th_seq);
            if (SEQ_LT(seq, last_seq)) {
                click_chatter("Unorderd %u %u %s", last_seq, seq, IPFlowID(p).unparse().c_str());
                ordered = false;
            }
            last_seq = seq;

            if (isJustAnAck(p))
                hasSimpleAck = true;
            else
                hasData = true;
        }

        int count = batch->count();

        if (_reorder && !ordered) {
            _state->reordered += 1;
            batch->tail()->set_next(0);

            batch = PacketBatch::make_from_simple_list(TCPReorder::sortList(batch->first()));
        }

        sf_assert(count == batch->count());
        sf_assert(batch->tail());
        sf_assert(batch->tail()->next() == 0);
        
        bool ackSent = false;
        tcp_seq_t last_ack = 0;
        tcp_seq_t last_ack_or = 0;
        auto fnt = [biggest_ack,&ackSent,&last_ack_or,&last_ack,biggest_p,hasData,this](Packet* p) -> Packet* {
            sf_assert(p->tcp_header());
            tcp_seq_t ack = ntohl(p->tcp_header()->th_ack);

            if (!ackSent && !hasData) { //If ack is not sent and all those packets are just acks, force to process
            } else { //Else, remove all simple acks
                if (isJustAnAck(p) && (last_ack_or != ack || SEQ_LT(ack,biggest_ack))) {//Do not remove DUP acks
                    _state->killed++;
                    p->kill();
                    return 0;
                }
            }
            last_ack = ack;
            //And always update ack of kept packets to the last known value
            WritablePacket* q = p->uniqueify();
            //q->rewrite_seq(htonl(biggest_ack), 1);
            last_ack_or = ntohl(q->tcp_header()->th_ack);
            if (biggest_ack != last_ack_or) {
                q->tcp_header()->th_ack = htonl(biggest_ack);
            //    last_ack_changed = true;
            } else {
              //  last_ack_changed = false;
            }

            q->tcp_header()->th_win = biggest_p->tcp_header()->th_win;
            resetTCPChecksum(q);
            ackSent = true;
            return q;
        };
        EXECUTE_FOR_EACH_PACKET_DROPPABLE(fnt,batch,(void));

        sf_assert(batch);
        sf_assert(batch->tail());
        sf_assert(batch->tail()->next() == 0);
        sf_assert(batch->count() == batch->find_count());
        sf_assert(batch->tail());
}

void SFMaker::run_timer(Timer* t)
{
    _state->task->reschedule();
}

inline void SFMaker::prepareBurst(SFSlot& f, PacketBatch* &all) {
    int count = all->count();
    f.packet_sent += count;
    f.burst_sent ++;

    if (count == 1)
        f.fail++;
    else
        f.fail--;
    _state->sent += count;
    if (_proto_compress) {
        sf_assert(all->first());
        sf_assert(all->first()->ip_header());
        if (all->first()->ip_header()->ip_p == IPPROTO_TCP)
            handleTCP(all);
    }
    sf_assert(all);

#if SF_ADVANCED_STATS
    SFState::Stat s;
    if (all->count() <= count) {
        s.compressed = count - all->count();
    } else {
        s.compressed = 0;
        click_chatter("Very bad error : protocol added %d packets", all->count() - count );
    }
#endif
}
/**
 * Schedule packets of a flow in the priority queue and set stats
 */
#if SF_PRIO
bool SFMaker::schedule_burst_from_flow(SFSlot& f, FlowQueue& q, TSCTimestamp& now, unsigned max) {
#else
bool SFMaker::schedule_burst_from_flow(SFSlot& f, TSCTimestamp& now, unsigned max) {
#endif
    (void)max;
    PacketBatch* all = f.dequeue();

    if (unlikely(!all)) {
        return false;
    }

    prepareBurst(f, all);
#if SF_PRIO
    if (_remanage) {
#if !SF_SLOT_IN_FCB
        assert(false); //TODO : keep a pointer if slot is not in FCB
#endif
        fcb_stack = stack_from_flow(&f);
    }
    Burst b = {.prio = f.prio(now, this), .batch = all, .fcb = fcb_stack};
    q.insert(b);
#else
    if (_remanage) {
        fcb_stack = stack_from_flow(&f);
        fcb_acquire(all->count());
    }
    output_push_batch(0, all);

#endif

    bool finished = f.empty();

    //Push stats
#if SF_ADVANCED_STATS
    if (f.last_seen > now) {
        s.useless_wait = 0;
    } else {
        s.useless_wait = (now-f.last_seen).usecval();
    }
    s.packets = count;

    s.bursts = f.bursts;
    _state->stats.push_back(s);
#endif

    //Reset data
    if (finished) {
        f.waiting_since = TSCTimestamp();

#if SF_ADVANCED_STATS
        f.bursts = 0;
#endif
    }

    return finished;
}


/**
 * SF Main loop
 */
bool SFMaker::run_task(Task* t)
{
    TSCTimestamp next = TSCTimestamp();

#if HAVE_FLOW
    if (!_remanage)
        assert(fcb_stack == 0);
#endif
#if SF_PRIO
    //Priority queue ordering bursts of different flows by emergency
    FlowQueue q = FlowQueue();
#endif

    //S is the state for this thread. It is not copied (passed by reference) for performance
    auto &s = *_state;

    //We're going to play with indexes, so we need to take the lock
#if SF_PIPELINE
    //STEP 1 : look for new indexes that have been allocated (in new_allocated) and add them in allocated
    s.idx_lock.acquire();
    Vector<int> new_allocated;
    new_allocated.swap(_new_allocated);
    s.idx_lock.release();

    int n = s.allocated.size();
    if (new_allocated.size() > 0) {
        s.allocated.resize(_allocated.size() + new_allocated.size());
        for (int i = 0; i < new_allocated.size(); i++) {
            s.allocated[n++] = new_allocated[i];
        }
    }
#else
    //STEP 1 : In parallel mode, new allocated slots are added to the allocated array directly
# if !SF_SLOT_IN_FCB
    int n = s.allocated.size();
# endif
#endif

    //STEP 2 : releasing slots that expired
    bool sent = false;
    TSCTimestamp now,loop_start = TSCTimestamp::now_steady();

#if SF_LLDS
    bool done = false;
start:
    while(!done){
        SFSlot* head = s.head_slot;
    
        // No SFSlot is in the linkedList (Reframer is empty)
        if (unlikely(head == 0)){
            goto single_packets_check;
        }
        // Check if the first Slot is ready
        SFSlot &f = *head;
        SFSlot *fptr = head;

        sf_assert(!f.empty());

#if SF_LLDS_SP
        sf_assert(!f.sp_inList);
#endif
        now = TSCTimestamp::now_steady();
        if (unlikely(!f.ready(now, this))){
            done = true;
            goto single_packets_check;
        }

        if (!f.lock.attempt()) {
            click_chatter("Lock attempt failed!");
            goto start;
        }

        now = TSCTimestamp::now_steady();

        #if SF_PRIO
            schedule_burst_from_flow(f, q, now, -1);
        #else
            schedule_burst_from_flow(f, now, -1);
        #endif
    
        sent = true;
        s.active--;
        // remove SFSlot from the list.
        removeFromList(f,s);
        f.lock.release();

        if (s.head_slot == 0){
            s.need_schedule = true;
            done = true;
            goto single_packets_check;
        }
    }
    
    single_packets_check:

#if SF_LLDS_SP
    if (_delay_last > 0) {
        done = false;
        while(!done){
            now = TSCTimestamp::now_steady();
            SFSlot* sp_head = s.sp_head;
            if( sp_head == 0)
                goto endLoop;
            SFSlot &f = *sp_head;
            sf_assert(!f.inList);
            if(sp_head == 0) {
                goto endLoop;
            }
            sf_assert(f.batch->count() == 1);
            if (now < f.sp_expiry(this)){
                done = true;
                goto endLoop;
            }

            #if SF_PRIO
            schedule_burst_from_flow(f, q, now, -1);
            #else
            schedule_burst_from_flow(f, now, -1);
            #endif
            sent = true;
            s.active--;
            removeFromSPList(f,s);

            if (s.sp_head == 0){
                done = true;
                goto endLoop;
            }
        }
    }

#endif
    endLoop:
     //Set next schedule time
    if(!_always){ 
        SFSlot* m_head = s.head_slot;

#if SF_LLDS_SP
        SFSlot* s_head = s.sp_head;
#endif

        if (m_head == 0

#if SF_LLDS_SP
                && s_head == 0
#endif
                )
            s.need_schedule = true;
        else

#if SF_LLDS_SP
            if (m_head == 0)
            next = s_head->sp_expiry(this);
        else if (s_head == 0)
            next = m_head->expiry(this);
        else {
                next = min(s_head->sp_expiry(this), m_head->expiry(this));
        }
#else
        if (m_head != 0)
            next = m_head->expiry(this);
#endif
    }
 
#else
    // N is all the allocate flows before we launched this function (in pipeline mode, new ones may be added afterwards)
    for (int j = 0; j < n; j++) {
        redo:
        int i = s.allocated[j];

        click_chatter("Checking allocated %d/%d", j, i);
        SFSlot &f = s.flows[i];

    // Look for released flow. If it timed out, the released flag will be set
        if (unlikely(f.released)) {
            f.lock.acquire();
            f.released = 0;
            f.lock.release();
            s.idx_lock.acquire();
            s.indexes.push_back(i);
            s.idx_lock.release();
            --n;
            if (j < n) {
                s.allocated[j] = s.allocated[n];
                goto redo;
            } else {
                break;
            }
        }

        if (likely(f.empty() ||!f.active())) {
            continue;
        }

        if (!f.lock.attempt()) {
            if (!_always) {
                TSCTimestamp exp = f.expiry(this);
                if (next == TSCTimestamp() || exp < next)
                    next = exp;
            }

            continue;
        }
        if (unlikely(_verbose > 4))
            click_chatter("Flow %d", i);

        now = TSCTimestamp::now_steady();

        if ((_take_all && !f.empty()) || f.ready(now, this)) { //Flow is ready to go
#if SF_PRIO
            schedule_burst_from_flow(f, q, now, -1);
#else
            schedule_burst_from_flow(f, now, -1);
            sent = true;
#endif
        }

        if (!_always && !f.empty()) {
            TSCTimestamp exp = f.expiry(this);
            if (next == TSCTimestamp() || exp < next)
                next = exp;
        }

        f.lock.release();
    }

    if (s.allocated.size() != n) {
        s.allocated.resize(n);
    }
    
#endif

#if SF_PRIO
    
    if (q.size() > 0 || s.ready_batch != 0)
    {
        TSCTimestamp send_begin = TSCTimestamp::now_steady();
        if (_verbose > 1) {
            click_chatter("Have %d frames", q.size());
        }
        s.sf++;
        s.sf_flows += q.size();
        sent = true;
        //PacketBatch* superframe = 0;
        int burst = 0;
        int c = 0;
        int split = 0;

        PacketBatch* batch = s.ready_batch;
        // PacketBatch* batch = 0;
        int i = 0;

        if (_remanage) {
            assert(!batch);
        }
        for(FlowQueue::iterator it = q.begin(); it != q.end(); ) {
            Burst &b = *const_cast<Burst*>(&(*it)); //Horrible but safe

            if (_remanage) { //We have to keep bursts together if we re-manage
                fcb_stack = b.fcb;
                fcb_acquire(b.batch->count());
                c += b.batch->count();
                output(0).push_batch(b.batch);
                burst++;

                s.last_tx_time = send_begin;
            } else {
                if (!batch)
                    batch = b.batch;
                else
                    batch->append_batch(b.batch);
                b.batch = 0;

                sf_assert(batch);

                while (batch->count() > _max_tx_burst) {
                    int count = batch->count();
                    batch->split(_max_tx_burst, b.batch);
                    sf_assert(batch->count() == _max_tx_burst);
                    sf_assert(batch->count() == batch->find_count());
                    sf_assert(b.batch->count() == count - _max_tx_burst);
                    sf_assert(b.batch->count() == b.batch->find_count());

                    if (_verbose > 3)
                        click_chatter("[%d] Sending burst of %d packets", i, batch->count());
                    i++;
                    c += batch->count();
                    output(0).push_batch(batch);
                    burst++;

                    batch = b.batch;
                    s.last_tx_time = send_begin;
                    b.batch = 0;
                }
            }
            it = q.erase(it);

        }
            
        if (batch) {
            if(batch->count() >= _min_tx_burst || (send_begin - s.last_tx_time).usecval() > _max_tx_delay) {
            if (_verbose > 3)
                    click_chatter("[%d] Sending burst of %d packets", i, batch->count());
                i++;
            c += batch->count();
            output(0).push_batch(batch);
            batch = 0;
            s.last_tx_time = send_begin;
                burst++;
            } else if (batch->count() > 0) {
                if (next == TSCTimestamp() || (next - s.last_tx_time).usecval() > _max_tx_delay)
                    next = send_begin + TSCTimestamp::make_usec(_max_tx_delay);
            }
        }
        s.ready_batch = batch;

        s.sf_size+=c;

        TSCTimestamp send_end = TSCTimestamp::now_steady();
        if (_verbose > 1 &&  c > 100) {
            click_chatter("%d packets sent in %s (total loop %s)",c, (send_end-send_begin).unparse().c_str(), (send_end-now).unparse().c_str());
        }
    //click_chatter("%d packets sent in %d (total loop %d)",c, (send_end-send_begin).usecval(), (send_end-now).usecval());
    }
#endif
    resched:
#if !SF_NO_SCHED
    if (_always) {
        s.task->fast_reschedule();
#if SF_LLDS
    } else if (!s.need_schedule && next < TSCTimestamp::now_steady()){
        s.task->fast_reschedule();
#endif
    } else {
        if (next != TSCTimestamp()) {
            s.timer->schedule_after((next - TSCTimestamp::now()));
        }
    }
#endif
    return sent;
}

void SFMaker::push_flow(int, SFFlow* flow, PacketBatch* batch)
{

    TSCTimestamp now = TSCTimestamp::now_steady();
#if !SF_SLOT_IN_FCB
    SFSlot &f = _state->flows[flow->index];
#else
    SFSlot &f = *flow;
#endif
    f.lock.acquire();

    int existing = 0;
    Packet* first = batch->first();
    bool bypass = false;

    if (f.last_seen == TSCTimestamp()) {
        if (_pass && _model == MODEL_SECOND) {
            bypass = true;
        }
    } else {
        if (_bypass_after_fail> 0 && f.fail >= _bypass_after_fail)
            bypass = true;
    }

    if (first->ip_header()->ip_p == IP_PROTO_TCP && first->tcp_header()->th_flags & TH_SYN) {
        if (unlikely(_verbose > 2))
            click_chatter("Unreset TCP");
        /*

        PacketBatch* ext = f.dequeue();
          if (ext) {

            click_chatter("SYN with queued DATA??? L:%d", ext->count());
            existing += ext->count();
            ext->append_batch(PacketBatch::make_from_simple_list(batch));
            batch = ext;
            //ext->kill();
            f.reset(TSCTimestamp::now_steady());
        }*/
        if (_bypass_syn)
            bypass = true;
    }

    int release = batch->count() - existing;
    //assert(fcb_stack->count() > release);
    fcb_release(release);
    _state->pushed += release;

    sf_assert(batch->tail()->next() == 0);

    f.last_seen = now;

    if (!bypass) {

        f.enqueue(batch);
        //Enqueue the packets in the slot
#if SF_LLDS

#if SF_LLDS_SP
        //Remove from SP LL
        if (_delay_last > 0

                && f.sp_inList
                ) {
            if(f.sp_next !=0)
                f.sp_next->sp_prev = f.sp_prev;
            else
                _state->sp_tail = f.sp_prev;

            if(f.sp_prev !=0)
                f.sp_prev->sp_next = f.sp_next;
            else
                _state->sp_head = f.sp_next;

            f.sp_inList = false;
            f.sp_next = 0;
            f.sp_prev = 0;
        }

#endif

#if SF_LLDS_SP
        if (_delay_last > 0 && f.batch->count() ==1){
            sf_assert(f.inList == false);
            sf_assert(f.sp_inList == false);
            f.sp_next = 0;
            f.sp_prev = _state->sp_tail;
            f.sp_inList = true;
            if(_state->sp_tail != 0)
                _state->sp_tail->sp_next = &f;
            _state->sp_tail = &f;
            if(_state->sp_head == 0)
                _state->sp_head = &f;
        } else

#endif
         if (_state->head_slot == 0){
            sf_assert(f.inList == false);
#if SF_LLDS_SP
            sf_assert(f.sp_inList == false);
#endif
                //add to LL as the first Node
            _state->head_slot = &f;
            _state->tail_slot = &f;
            f.next = 0;
            f.prev = 0;
            f.inList = true;
        _state->active++;

        } else if (!f.inList){
            //add to tail
            f.next = 0;
            f.prev = _state->tail_slot;
            _state->tail_slot->next = &f;
            _state->tail_slot = &f;
            f.inList = true;
           _state->active++;
        }

        if (_max_capacity != -1 && _state->active > _max_capacity){
                SFSlot* head_slot = _state->head_slot;
#if SF_LLDS_SP
            SFSlot* sp_head = -state->sp_head;
                if(sp_head != 0 && head_slot->waiting_since > sp_head->waiting_since)
                head_slot = sp_head;
#endif
            sf_assert(head_slot != 0);
            head_slot->forced_flush = true;
        }
        

        if (f.batch->count() > _max_burst) {
        //        click_chatter("%d packets. inburst: %d, received batcah: %d, pointer: %p, next: %p, waiting_for: %d us", f.batch->count() , f.inList, batch->count(), &f, f.next, (now - f.waiting_since).usecval());
            if (f.prev != 0){
                //REMOVE NODE FROM THE CHAIN
                if (f.next !=0)
                    f.next->prev = f.prev;
                else
                    _state->tail_slot = f.prev;
                f.prev->next = f.next;
                //ADD NODE TO HEAD
                f.next = _state->head_slot;
                f.prev = 0;
                _state->head_slot = &f;
            }
        }

#endif

        //Update stats
#if SF_ADVANCED_STATS
        f.bursts++;
#endif
        if (f.waiting_since == TSCTimestamp())
            f.waiting_since = now;

        TSCTimestamp exp = f.expiry(this);
#if !SF_NO_SCHED
    if (!_always) {
# if SF_LLDS
        if (_state->need_schedule)
# else
        if (!_state->timer->scheduled()) //TODO : if slo varies, then we may want to schedule earlier
# endif
        {
            if (unlikely(_verbose > 1))
                click_chatter("Schedule in %d usec (delay %d, waiting since %d)",(exp-now).usecval(), _delay.usecval(),(now-f.waiting_since).usecval());
            _state->timer->schedule_after(exp - TSCTimestamp::now());
# if SF_LLDS
            _state->need_schedule = false;
# endif
        }
    }
#endif

    } else { //if bypass

        auto &s = *_state;
        if (f.inList) {
            removeFromList(f,s);
        }

#if SF_LLDS_SP
        if (f.sp_inList) {
            removeFromSPList(f,s);
        }
#endif

        prepareBurst(f, batch);
        output(0).push_batch(batch);
    }

    f.lock.release();
    //Schedule timer
#if !SF_NO_SCHED

    if (!_always) {
#if SF_LLDS
        if (_state->need_schedule)
#else
        if (!_state->timer->scheduled()) //TODO : if slo varies, then we may want to schedule earlier
#endif
        {

        } else {
#endif
            SFSlot* m_head = _state->head_slot;

#if SF_LLDS_SP
            SFSlot* s_head = _state->sp_head;
#endif
            bool ready = false;
            if (m_head != 0 && m_head->ready(now, this))
                ready = true;

#if SF_LLDS_SP
            if (_delay_last > 0 &&  s_head != 0 && now < s_head->sp_expiry(this))
                ready = true;
#endif
            if (ready){
                _state->timer->clear();
                 SFCB_STACK(run_task(_state->task));
            }
#if !SF_NO_SCHED
        }
    }
#endif
}

void SFMaker::release_flow(SFFlow* flow) {

    if (_verbose > 1) {

#if SF_SLOT_IN_FCB
        click_chatter("Flow released");
#else

        click_chatter("Flow %d released", flow->index);
#endif
    }

    //Kill flows that would be still in the queue. Should not happen

#if !SF_SLOT_IN_FCB
    auto &f = _state->flows[flow->index];
#else
    auto &f = *flow;
#endif
    f.lock.acquire();
    if (!f.empty()) {
        f.dequeue()->kill();
        click_chatter("BUG : a flow timed out with some packets in its ring. Expire in %dusec", (TSCTimestamp::now_steady() - f.expiry(this)).usecval());
    }

    //Release the slot


    f.released = 1;
    f.first_seen = TSCTimestamp();
 //       click_chatter("Release %d", index);

    f.lock.release();

}

inline bool SFMaker::new_flow(SFFlow* flow, Packet*) {

#if !SF_SLOT_IN_FCB
    //Get a slot
    flow->index = allocate_index();
    if (_verbose > 1)
        click_chatter("%p{element} Flow %d allocated", this, flow->index);

    //Reset values
    SFSlot &f = _state->flows[flow->index];
#else
    SFSlot &f = *flow;
#endif

    //sf_assert(_flows[flow->index].lock.free());
    f.lock.acquire();
    f.reset(TSCTimestamp::now_steady());
    f.lock.release();

#if !SF_SLOT_IN_FCB
    _state->idx_lock.acquire();
#if SF_PIPELINE
    //_new_allocated.push_back(flow->index);
#else
    _state->allocated.push_back(flow->index);
#endif
    _state->idx_lock.release();
#endif
    fcb_acquire(1);

    return true;
}

enum handlers_t {
    AC_PACKETS_AVG, AC_PACKETS, AC_BURSTS_AVG, AC_USELESS_WAIT_AVG, AC_SF, AC_COMPRESS_AVG, AC_SF_FLOWS_AVG, AC_SF_SIZE_AVG, AC_ACTIVE, AC_QUEUED, AC_PUSHED, AC_SENT, AC_KILLED, AC_REORDERED, AC_FLUSH
};

String
SFMaker::read_handler(Element *e, void *thunk)
{
    SFMaker *sf = static_cast<SFMaker *>(e);
    auto &s = *sf->_state;
    int t = (intptr_t)thunk;
    switch (t) {
      case AC_ACTIVE:
          return String(s.active);
      case AC_QUEUED:
          return String(s.pushed - s.sent);
      case AC_PUSHED:
          return String(s.pushed);
      case AC_SENT:
          return String(s.sent);
      case AC_KILLED:
          return String(s.killed);
      case AC_REORDERED:
          return String(s.reordered);
      case AC_SF:
          return String(s.sf);
      case AC_SF_SIZE_AVG:
          return String((double)s.sf_size / s.sf);
      case AC_SF_FLOWS_AVG:
          return String((double)s.sf_flows / s.sf);
#if SF_ADVANCED_STATS
      case AC_PACKETS: {
          uint64_t total = 0;
          for (int i = 0; i < s.stats.size(); i++) {
              if (t == AC_PACKETS)
                  total += s.stats[i].packets;
          }
          return String(total);
      }
      case AC_PACKETS_AVG:
      case AC_BURSTS_AVG:
      case AC_COMPRESS_AVG:
      case AC_USELESS_WAIT_AVG: {
          uint64_t total = 0;
          for (int i = 0; i < s.stats.size(); i++) {
              if (t == AC_PACKETS_AVG)
                  total += s.stats[i].packets;
              else if (t == AC_COMPRESS_AVG)
                  total += s.stats[i].compressed;
              else if (t == AC_BURSTS_AVG)
                  total += s.stats[i].bursts;
              else
                  total += s.stats[i].useless_wait;
          }
          uint64_t n = s.stats.size();
          return String((double)total / (double)n);
      }
    #endif
      default:
          return "<error>";
    }
}

int SFMaker::write_handler(const String &, Element *e, void *thunk, ErrorHandler *)
{
    SFMaker *sf = static_cast<SFMaker *>(e);
    auto &s = *sf->_state;
    int t = (intptr_t)thunk;
    switch (t) {
      case AC_FLUSH:
          sf->_delay = TSCTimestamp::make_usec(0);
          sf->_delay_last = TSCTimestamp::make_usec(0);
          sf->run_task(s.task);
    }
    return 0;
}

void
SFMaker::add_handlers()
{
    add_read_handler("compress_avg", read_handler, AC_COMPRESS_AVG);
    add_read_handler("superframes", read_handler, AC_SF);
    add_read_handler("reordered", read_handler, AC_REORDERED);

    add_read_handler("superframe_size_avg", read_handler, AC_SF_SIZE_AVG);
    add_read_handler("superframe_flows_avg", read_handler, AC_SF_FLOWS_AVG);
    add_read_handler("packets_avg", read_handler, AC_PACKETS_AVG);
    add_read_handler("bursts_avg", read_handler, AC_BURSTS_AVG);
    add_read_handler("useless_wait_avg", read_handler, AC_USELESS_WAIT_AVG);

    add_read_handler("queued", read_handler, AC_QUEUED);
    add_read_handler("pushed", read_handler, AC_PUSHED);
    add_read_handler("count", read_handler, AC_PACKETS);
    add_read_handler("sent", read_handler, AC_SENT);
    add_read_handler("dropped", read_handler, AC_KILLED);
    add_read_handler("active", read_handler, AC_ACTIVE);

    add_write_handler("flush", write_handler, AC_FLUSH);
}


CLICK_ENDDECLS
ELEMENT_REQUIRES(!ctx-global-timeout)
EXPORT_ELEMENT(SFMaker)
ELEMENT_MT_SAFE(SFMaker)
