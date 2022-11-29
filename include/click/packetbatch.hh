// -*- related-file-name: "../../lib/packetbatch.cc" -*-
#ifndef CLICK_PACKETBATCH_HH
#define CLICK_PACKETBATCH_HH

#include <click/packet.hh>
CLICK_DECLS

/**
 * Iterate over all packets of a batch. The batch cannot be modified during
 *   iteration. Use _SAFE version if you want to modify it on the fly.
 */
#define FOR_EACH_PACKET_LL(first,p) for(Packet* p = first;p != 0;p=p->next())

/**
 * Iterate over all packets of a batch. The batch cannot be modified during
 *   iteration. Use _SAFE version if you want to modify it on the fly.
 */
#define FOR_EACH_PACKET(batch,p) FOR_EACH_PACKET_LL(batch->first(),p)

/**
 * Iterate over all packets of a batch. The current packet can be modified
 *  during iteration as the "next" pointer is read before going in the core of
 *  the loop.
 */
#define FOR_EACH_PACKET_LL_SAFE(first,p) \
                Packet* fep_next = ((first != 0)? first->next() : 0 );\
                Packet* p = first;\
                for (;p != 0;p=fep_next,fep_next=(p==0?0:p->next()))


#define FOR_EACH_PACKET_SAFE(batch,p) FOR_EACH_PACKET_LL_SAFE(batch->first(),p)

/**
 * Execute a function on each packets of a batch. The function may return
 * another packet to replace the current one. This version cannot drop !
 * Use _DROPPABLE version if the function could return null.
 */
#define EXECUTE_FOR_EACH_PACKET(fnt,batch) \
                Packet* efep_next = ((batch != 0)? batch->first()->next() : 0 );\
                Packet* p = batch->first();\
                Packet* last = 0;\
                for (;p != 0;p=efep_next,efep_next=(p==0?0:p->next())) {\
                    Packet* q = fnt(p);\
                    if (q != p) {\
                        if (last) {\
                            last->set_next(q);\
                        } else {\
                            batch = reinterpret_cast<PacketBatch*>(q);\
                        }\
                        q->set_next(efep_next);\
                    }\
                    last = q;\
                }

/**
 * Execute a function that returns a bool on each packets of a batch.
 * The function may take the packet by reference and change the reference.
 * If the function returns false, the loop stops and on_stop is called
 * with the whole batch in argument, the packet causing the stop, and the next
 * reference. This function does not kill any packet by itself.
 */
#define EXECUTE_FOR_EACH_PACKET_UNTIL_DO(fnt,batch,on_stop) \
                Packet* efep_next = ((batch != 0)? batch->first()->next() : 0 );\
                Packet* p = batch->first();\
                Packet* last = 0;\
                int count = batch->count();\
                for (;p != 0;p=efep_next,efep_next=(p==0?0:p->next())) {\
                    Packet* q = p;\
                    bool drop = !fnt(q);\
                    if (q != p) {\
                        if (last) {\
                            last->set_next(q);\
                        } else {\
                            batch = reinterpret_cast<PacketBatch*>(q);\
                            batch->set_count(count);\
                        }\
                        q->set_next(efep_next);\
                    }\
                    if (unlikely(drop)) {\
                        on_stop(batch, q, efep_next);\
                        break;\
                    }\
                    last = q;\
                }

//Variant that will drop the whole batch when fnt return false
#define EXECUTE_FOR_EACH_PACKET_UNTIL(fnt,batch) \
    EXECUTE_FOR_EACH_PACKET_UNTIL_DO(fnt, batch, [](PacketBatch*& batch, Packet*, Packet*){batch->kill();batch = 0;})

/*
 * Variant that will drop the remaining packets, but return the batch up to the drop (the packet for which fnt returned true is included.
 * A usage example is a NAT, that translate all packets up to when the state is destroyed. But sometimes there could be unordered packets still coming after the last ACK, or duplicate FIN.
 */
#define EXECUTE_FOR_EACH_PACKET_UNTIL_DROP(fnt,batch) \
    EXECUTE_FOR_EACH_PACKET_UNTIL_DO(fnt, batch, [](PacketBatch*& batch, Packet* dropped, Packet* next){ if (!next) return; PacketBatch* remain = PacketBatch::make_from_simple_list(next);batch->set_count(batch->count() - remain->count()); batch->set_tail(dropped); dropped->set_next(0); remain->kill(); })

/**
 * Execute a function on each packet of a batch.
 * The batch will be modified in-place according to the output of the function.
 *
 * The function may return
 * another packet and which case the packet of the batch will be replaced by
 * that one, or null if the packet is to be dropped.
 *
 * If all packets are dropped, batch will become null. If the first packets are dropped, the address of batch will change.
 *
 *
 * Example: EXECUTE_FOR_EACH_PACKET_DROPPABLE([this](Packet* p){return p->push(_nbytes);},batch,[](Packet* p){})
 */
#define EXECUTE_FOR_EACH_PACKET_DROPPABLE(fnt,batch,on_drop) {\
                Packet* efepd_next = ((batch != 0)? batch->first()->next() : 0 );\
                Packet* p = batch->first();\
                Packet* last = 0;\
                int count = batch->count();\
                for (;p != 0;p=efepd_next,efepd_next=(p==0?0:p->next())) {\
            Packet* q = fnt(p);\
            if (q == 0) {\
                on_drop(p);\
                if (last) {\
                    last->set_next(efepd_next);\
                } else {\
                    batch = PacketBatch::start_head(efepd_next);\
                }\
                        count--;\
                        continue;\
            } else if (q != p) {\
                        if (last) {\
                            last->set_next(q);\
                        } else {\
                            batch = reinterpret_cast<PacketBatch*>(q);\
                        }\
                        q->set_next(efepd_next);\
                    }\
                    last = q;\
                }\
                if (batch) {\
                    batch->set_count(count);\
                    batch->set_tail(last);\
                    last->set_next(0);\
                }\
            }\

/**
 * Same as EXECUTE_FOR_EACH_PACKET_DROPPABLE but build a list of dropped packet
 * instead of calling a function
 */
#define EXECUTE_FOR_EACH_PACKET_DROP_LIST(fnt,batch,drop_list) \
        PacketBatch* drop_list = 0;\
        auto on_drop = [&drop_list](Packet* p) {\
            if (drop_list == 0) {\
                drop_list = PacketBatch::make_from_packet(p);\
            } else {\
                drop_list->append_packet(p);\
            }\
        };\
        EXECUTE_FOR_EACH_PACKET_DROPPABLE(fnt,batch,on_drop);


/**
 * Execute a function on each packet of a batch. The function may return
 * the same packet if nothing is to change,
 * another packet in which case it will repair the batch,
 * or null. If it returns null, the batch
 * is flushed using on_flush and processing continue on a new batch starting
 * at the next packet. on_drop is called on the packet that was returned
 * as null after flushing.
 * On_flush is always called on the batch after the last packet.
 */
#define EXECUTE_FOR_EACH_PACKET_SPLITTABLE(fnt,batch,on_drop,on_flush) {\
            Packet* next = ((batch != 0)? batch->first()->next() : 0 );\
            Packet* p = batch->first();\
            Packet* last = 0;\
            int count = 0;\
            for (;p != 0;p=next,next=(p==0?0:p->next())) {\
                Packet* q = (fnt(p));\
                if (q == 0) {\
                    if (last) {\
                        batch->set_count(count);\
                        batch->set_tail(last);\
                        on_flush(batch);\
                    }\
                    on_drop(p);\
                    count = 0;\
                    last = 0;\
                    batch = PacketBatch::start_head(next);\
                    continue;\
                } else if (q != p) {\
                    if (last) {\
                        last->set_next(q);\
                    } else {\
                        batch = reinterpret_cast<PacketBatch*>(q);\
                    }\
                    q->set_next(next);\
                }\
                last = q;\
                count++;\
            }\
            if (last) {\
                batch->set_count(count);\
                batch->set_tail(last);\
                on_flush(batch);\
            }\
        }\


/**
 * Split a batch into multiple batch according to a given function which will
 * give the index of an output to choose.
 *
 * The main use case is the classification element like Classifier, Switch, etc
 *   where you split a batch checking which output each packets should take.
 *
 * @args nbatches Number of output batches. In many case you want noutputs() + 1
 *      , keeping the last one for drops.
 * @args fnt Function to call which will return a value between 0 and nbatches.
 *  If the function returns a values < 0 or bigger than nbatches, th last batch
 *  of nbatches will be used.
 * @args batch The batch to be split
 * @args on_finish function which take an output index and the batch when
 *  classification is finished, usually you want that to be
 *  checked_output_push_batch.
 */
#define CLASSIFY_EACH_PACKET(nbatches,fnt,cep_batch,on_finish)\
    {\
        PacketBatch* out[(nbatches)];\
        bzero(out,sizeof(PacketBatch*)*(nbatches));\
        PacketBatch* cep_next = ((cep_batch != 0)? reinterpret_cast<PacketBatch*>(cep_batch->first()->next()) : 0 );\
        Packet* p = cep_batch->first();\
        Packet* last = 0;\
        int last_o = -1;\
        int passed = 0;\
        for (;p != 0;p=cep_next->first(),cep_next=(p==0?0:reinterpret_cast<PacketBatch*>(p->next()))) {\
            int o = (fnt(p));\
            if (o < 0 || o>=(int)(nbatches)) o = (nbatches - 1);\
            if (o == last_o) {\
                passed ++;\
            } else {\
                if (last == 0) {\
                    out[o] = reinterpret_cast<PacketBatch*>(p);\
                    reinterpret_cast<PacketBatch*>(p)->set_count(1);\
                    reinterpret_cast<PacketBatch*>(p)->set_tail(p);\
                } else {\
                    out[last_o]->set_tail(last);\
                    out[last_o]->set_count(out[last_o]->count() + passed);\
                    if (!out[o]) {\
                        out[o] = reinterpret_cast<PacketBatch*>(p);\
                        out[o]->set_count(1);\
                        out[o]->set_tail(p);\
                    } else {\
                        out[o]->append_packet(p);\
                    }\
                    passed = 0;\
                }\
            }\
            last = p;\
            last_o = o;\
        }\
\
        if (passed) {\
            out[last_o]->set_tail(last);\
            out[last_o]->set_count(out[last_o]->count() + passed);\
        }\
\
        unsigned i = 0;\
        for (; i < (unsigned)(nbatches); i++) {\
            if (out[i]) {\
                out[i]->tail()->set_next(0);\
                (on_finish(i,out[i]));\
            }\
        }\
    }

/**
 * Equivalent to CLASSIFY_EACH_PACKET but ignore the packet if fnt returns -1
 */

#define CLASSIFY_EACH_PACKET_IGNORE(nbatches,fnt,cep_batch,on_finish)\
    {\
        PacketBatch* out[(nbatches)];\
        bzero(out,sizeof(PacketBatch*)*(nbatches));\
        PacketBatch* cep_next = ((cep_batch != 0)? reinterpret_cast<PacketBatch*>(cep_batch->first()->next()) : 0 );\
        Packet* p = cep_batch->first();\
        Packet* last = 0;\
        int last_o = -1;\
        int passed = 0;\
        for (;p != 0;p=cep_next->first(),cep_next=(p==0?0:reinterpret_cast<PacketBatch*>(p->next()))) {\
            int o = (fnt(p));\
            if (o>=(nbatches)) o = (nbatches - 1);\
            if (o == last_o) {\
                passed ++;\
            } else {\
                if (last == 0) {\
                    if (o == -1) continue;\
                    out[o] = reinterpret_cast<PacketBatch*>(p);\
                    out[o]->set_count(1);\
                    out[o]->set_tail(p);\
                } else {\
                    if (last_o != -1) {\
                        out[last_o]->set_tail(last);\
                        out[last_o]->set_count(out[last_o]->count() + passed);\
                    }\
                    if (o != -1) {\
                        if (!out[o]) {\
                            out[o] = reinterpret_cast<PacketBatch*>(p);\
                            out[o]->set_count(1);\
                            out[o]->set_tail(p);\
                        } else {\
                            out[o]->append_packet(p);\
                        }\
                    }\
                    passed = 0;\
                }\
            }\
            last = p;\
            last_o = o;\
        }\
\
        if (passed && last_o != -1) {\
            out[last_o]->set_tail(last);\
            out[last_o]->set_count(out[last_o]->count() + passed);\
        }\
\
        int i = 0;\
        for (; i < (nbatches); i++) {\
            if (out[i]) {\
                out[i]->tail()->set_next(0);\
                (on_finish(i,out[i]));\
            }\
        }\
    }


/**
 * Create a batch by calling multiple times (up to max) a given function and
 *   linking them together in respect to the PacketBatch semantic.
 *
 * In most case this function should not be used. Because if you get packets
 * per packets it means you don't get them upstream as a batch. You may prefer
 * to somehow fetch a whole batch and then iterate through it. One bad
 * use case is MAKE_BATCH(pull ...) in a x/x element which will create a batch
 * by calling multiple times pull() until it returns no packets.
 * This will break batching as it will call pull on the previous element
 * instead of pull_batch. However this is fine in a source element where
 * anyway the batch must be created packet per packet.
 */
#define MAKE_BATCH(fnt,head,max) {\
        head = PacketBatch::start_head(fnt);\
        if (head != 0) {\
            Packet* last = head->first();\
            unsigned int count = 1;\
            while (count < (unsigned)(max>0?max:BATCH_MAX_PULL)) {\
                Packet* current = fnt;\
                if (current == 0)\
                    break;\
                last->set_next(current);\
                last = current;\
                count++;\
            }\
            head->make_tail(last,count);\
        }\
}

/**
 * Batch of Packet.
 * This class has no field member and can be cast to or from Packet. It is
 *  only there as a way to remember what we are handling and provide useful
 *  functions for managing packets as a batch.
 *
 * Internally, the head contains all the information usefull for the batch. The
 *  prev annotation points to the tail, the next to the next packet. It is
 *  implemented by a *simply* linked list. The BATCH_COUNT annotation is set
 *  on the first packet of the batch to remember the number of packets in the
 *  batch.
 *
 * Batches must not mix cloned and unique packets. Use cut to split batches and have part of them cloned.
 */
class PacketBatch {

//Consider a batch size bigger as bogus (prevent infinite loop on bad pointer manipulation)
#define MAX_BATCH_SIZE 8192

public :
    /*
     * Return the first packet of the batch
     */
    inline Packet* first() {
        return (Packet*)this;
    }

    /*
     * Set the tail of the batch
     */
    inline void set_tail(Packet* p) {
        first()->set_prev(p);
    }

    /*
     * Return the tail of the batch
     */
    inline Packet* tail() {
        return first()->prev();
    }
    
    /*
     * Append a simply-linked list of packet to the batch.
     * One must therefore pass the tail and the number of packets to do it in constant time. Chances are you
     * just created that list and can track taht.
     */
    inline void append_simple_list(Packet* lhead, Packet* ltail, int lcount) {
        tail()->set_next(lhead);
        set_tail(ltail);
        ltail->set_next(0);
        set_count(count() + lcount);
    }

    /*
     * Append a proper PacketBatch to this batch.
     */
    inline void append_batch(PacketBatch* head) {
        tail()->set_next(head->first());
        set_tail(head->tail());
        set_count(count() + head->count());
    }

    /*
     * Append a packet to the list.
     */
    inline void append_packet(Packet* p) {
        tail()->set_next(p);
        set_tail(p);
        set_count(count() + 1);
    }

    /**
     * Return the number of packets in this batch
     */
    inline unsigned count() {
        unsigned int r = BATCH_COUNT_ANNO(first());
        assert(r); //If this is a batch, this anno has to be set
        return r;
    }

    /**
     * @brief Start a new batch
     *
     * @param p A packet
     *
     * Creates a new batch, with @a p as the first packet. Batch is *NOT* valid
     *  until you call make_tail().
     * If the Packet is null, returns no batch.
     */
    inline static PacketBatch* start_head(Packet* p) {
        return reinterpret_cast<PacketBatch*>(p);
    }

    /**
     * @brief Finish a batch started with start_head()
     *
     * @param last The last packet of the batch
     * @param count The number of packets in the batch
     *
     * @return The whole packet batch
     *
     * This will set up the batch with the last packet. set_next() have to be called for each packet from the head to the @a last packet !
     */
    inline PacketBatch* make_tail(Packet* last, unsigned int count) {
        set_count(count);
        if (last == 0) {
            if (count != 1)
                click_chatter("BUG in make_tail : last packet is the head, but count is %u",count);
            set_tail(first());
            first()->set_next(0);
        } else {
            set_tail(last);
            last->set_next(0);
        }
        return this;
    }

    /**
     * Set the number of packets in this batch
     */
    inline void set_count(unsigned int c) {
        SET_BATCH_COUNT_ANNO(first(),c);
    }

    /**
     * @brief Cut a batch in two batches
     *
     * @param middle The last packet of the first batch
     * @param first_batch_count The number of packets in the first batch
     * @param second Reference to set the head of the second batch
     */
    inline void cut(Packet* middle, int first_batch_count, PacketBatch* &second) {
        if (middle == 0) {
            second = 0;
            click_chatter("BUG Warning : cutting a batch without a location to cut !");
            return;
        }

        if (middle == tail()) {
            second = 0;
            return;
        }

        int total_count = count();

        second = reinterpret_cast<PacketBatch*>(middle->next());
        middle->set_next(0);

        Packet* second_tail = tail();
        set_tail(middle);

        second->set_tail(second_tail);
        second->set_count(total_count - first_batch_count);

        set_count(first_batch_count);
    }

    /**
     * @brief Cut a batch in two batches
     *
     * @param first_batch_count The number of packets in the first batch
     * @param second Reference to set the head of the second batch
     * @param safe Set to true for optimization if you're sure there is enough packets to cut, and first_batch_count is not 0
     */
    inline void split(int first_batch_count, PacketBatch* &second, const bool &safe = false) {
        Packet* middle = first();
        if (unlikely(!safe)) {
            assert(first_batch_count > 0);
        }
        for (int i = 0; i < first_batch_count - 1; i++) {
            middle = middle->next();
            if (unlikely(!safe && middle == 0)) {
                second = 0;
                break;
            }
        }

        int total_count = count();

        second = reinterpret_cast<PacketBatch*>(middle->next());
        middle->set_next(0);

        Packet* second_tail = tail();
        set_tail(middle);

        second->set_tail(second_tail);
        second->set_count(total_count - first_batch_count);

        set_count(first_batch_count);
    }

    inline PacketBatch* split(int first_batch_count) {
                PacketBatch* second;
                split(first_batch_count,second, false);
                return second;
    }

    /**
     * Remove the first packet
     * @return the new batch without front. Do not use "this" afterwards!
     */
    PacketBatch* pop_front() {
        if (count() == 1)
            return 0;
        PacketBatch* poped = PacketBatch::start_head(first()->next());
        poped->set_count(count() -1 );
        poped->set_tail(tail());
        return poped;
    }

    /**
     * Build a batch from a linked list of packet for which head->prev is the tail and tail->next is already 0
     *
     * @param head The first packet of the batch
     * @param size Number of packets in the linkedlist
     *
     * @pre The "prev" annotation of the first packet must point to the last packet of the linked list
     * @pre The tail->next() packet must be zero
     */
    inline static PacketBatch* make_from_tailed_list(Packet* head, unsigned int size) {
        PacketBatch* b = reinterpret_cast<PacketBatch*>(head);
        b->set_count(size);
        return b;
    }

    /**
     * Build a batch from a linked list of packet
     *
     * @param head The first packet of the batch
     * @param tail The last packet of the batch
     * @param size Number of packets in the linkedlist
     */
    inline static PacketBatch* make_from_simple_list(Packet* head, Packet* tail, unsigned int size) {
        PacketBatch* b = make_from_tailed_list(head,size);
        b->set_tail(tail);
        tail->set_next(0);
        return b;
    }

    /**
     * Build a batch from a linked list of packet ending by a next==0 pointer. O(n).
     *
     * @param head The first packet of the batch
     */
    inline static PacketBatch* make_from_simple_list(Packet* head) {
        int size = 1;
        Packet* next = head->next();
        Packet* tail = head;
        while (next != 0) {
            size++;
            tail = next;
            next = tail->next();
        }
        PacketBatch* b = make_from_tailed_list(head,size);
        b->set_tail(tail);
        return b;
    }



    /**
     * Make a batch composed of a single packet
     */
    inline static PacketBatch* make_from_packet(Packet* p) {
        if (!p) return 0;
        PacketBatch* b = reinterpret_cast<PacketBatch*>(p);
        b->set_count(1);
        b->set_tail(p);
        p->set_next(0);
        return b;
    }

#if !CLICK_LINUXMODULE
    static PacketBatch *make_batch(unsigned char *data, uint16_t count, uint16_t *length,
                    Packet::buffer_destructor_type destructor,
                                    void* argument = (void*) 0, const bool clear=true) CLICK_WARN_UNUSED_RESULT;
#endif

    /**
     * Return the first packet of this batch
     */
    inline Packet* begin() {
        return first();
    }

    /**
     * Return the last packet of this batch
     */
    inline Packet* end() {
        return tail();
    }

    /**
     * Kill all packets in the batch
     */
    inline void kill();

    /**
     * Clone the batch
     */
    inline PacketBatch* clone_batch() {
        PacketBatch* head = 0;
        Packet* last = 0;
        FOR_EACH_PACKET(this, p) {
            Packet* q = p->clone();
            if (last == 0) {
                head = start_head(q);
                last = q;
            } else {
                last->set_next(q);
                last = q;
            }
        }
        return head->make_tail(last,count());
    }

#if HAVE_BATCH && HAVE_CLICK_PACKET_POOL
    /**
     * Kill all packets of batch of unshared packets. Using this on unshared packets is very dangerous !
     */
    void recycle_batch(bool is_data);

    void fast_kill();
    void fast_kill_nonatomic();
#else
    inline void fast_kill() {
        kill();
    }

    void fast_kill_nonatomic() {
        kill();
    }
#endif
};

/**
 * Recycle a whole batch
 */
inline void PacketBatch::kill() {
    FOR_EACH_PACKET_SAFE(this,p) {
        p->kill();
    }
}

#if HAVE_BATCH_RECYCLE
#define BATCH_RECYCLE_START() \
	WritablePacket* head_packet = 0;\
	WritablePacket* head_data = 0;\
	WritablePacket* last_packet = 0;\
	WritablePacket* last_data = 0;\
	unsigned int n_packet = 0;\
	unsigned int n_data = 0;

#define BATCH_RECYCLE_ADD_PACKET(p) {\
	if (head_packet == 0) {\
		head_packet = static_cast<WritablePacket*>(p);\
		last_packet = static_cast<WritablePacket*>(p);\
	} else {\
		last_packet->set_next(p);\
		last_packet = static_cast<WritablePacket*>(p);\
	}\
	n_packet++;}

#define BATCH_RECYCLE_ADD_DATA_PACKET(p) {\
	if (head_data == 0) {\
		head_data = static_cast<WritablePacket*>(p);\
		last_data = static_cast<WritablePacket*>(p);\
	} else {\
		last_data->set_next(p);\
		last_data = static_cast<WritablePacket*>(p);\
	}\
	n_data++;}

#define BATCH_RECYCLE_PACKET(p) {\
			if (p->shared()) {\
				p->kill();\
			} else {\
				BATCH_RECYCLE_UNKNOWN_PACKET(p);\
			}\
		}

#define BATCH_RECYCLE_PACKET_NONATOMIC(p) {\
            if (p->shared_nonatomic()) {\
                p->kill_nonatomic();\
            } else {\
                BATCH_RECYCLE_UNKNOWN_PACKET(p);\
            }\
        }

#if HAVE_DPDK_PACKET_POOL
#define BATCH_RECYCLE_UNKNOWN_PACKET(p) {\
	if (p->data_packet() == 0 && p->buffer_destructor() == DPDKDevice::free_pkt && p->buffer() != 0) {\
		BATCH_RECYCLE_ADD_DATA_PACKET(p);\
	} else {\
		BATCH_RECYCLE_ADD_PACKET(p);}}
#elif !defined(CLICK_NOINDIRECT)
#define BATCH_RECYCLE_UNKNOWN_PACKET(p) {\
	if (p->data_packet() == 0 && p->buffer_destructor() == 0 && p->buffer() != 0) {\
		BATCH_RECYCLE_ADD_DATA_PACKET(p);\
	} else {\
	    BATCH_RECYCLE_ADD_PACKET(p);}}
#else
#define BATCH_RECYCLE_UNKNOWN_PACKET(p) {\
	if (p->buffer_destructor() == 0 && p->buffer() != 0) {\
		BATCH_RECYCLE_ADD_DATA_PACKET(p);\
	} else {\
	    BATCH_RECYCLE_ADD_PACKET(p);}}
#endif

#define BATCH_RECYCLE_END() \
	if (last_packet) {\
		last_packet->set_next(0);\
		PacketBatch::make_from_simple_list(head_packet,last_packet,n_packet)->recycle_batch(false);\
	}\
	if (last_data) {\
		last_data->set_next(0);\
		PacketBatch::make_from_simple_list(head_data,last_data,n_data)->recycle_batch(true);\
	}
#else
#define BATCH_RECYCLE_START() {}
#define BATCH_RECYCLE_END() {}
#define BATCH_RECYCLE_PACKET(p) {p->kill();}
#define BATCH_RECYCLE_PACKET_NONATOMIC(p) {p->kill_nonatomic();}
#endif

/**
 * Use the context of the element to know if the NONATOMIC or ATOMIC version should be called
 */
#define BATCH_RECYCLE_PACKET_CONTEXT(p) {\
            if (likely(is_fullpush())) {\
                BATCH_RECYCLE_PACKET_NONATOMIC(p);\
            } else {\
                BATCH_RECYCLE_PACKET(p);\
            }\
        }

/**
 * Set of functions to efficiently create a batch.
 */
#define BATCH_CREATE_INIT(batch) \
        PacketBatch* batch = 0; \
        int batch ## count = 0; \
        Packet* batch ## last = 0;
#define BATCH_CREATE_APPEND(batch,p) \
        if (batch) { \
            batch ## last->set_next(p); \
        } else {\
            batch = PacketBatch::start_head(p); \
        }\
        batch ## last = p;\
        batch ## count++;
#define BATCH_CREATE_FINISH(batch) \
        if (batch) \
            batch->make_tail(batch ## last, batch ## count)

typedef Packet::PacketType PacketType;

#if HAVE_BATCH && HAVE_CLICK_PACKET_POOL
/**
 * Recycle a whole batch of unshared packets of the same type
 *
 * @precond No packet are shared
 */
inline void PacketBatch::recycle_batch(bool is_data) {
    if (is_data) {
        WritablePacket::recycle_data_batch((WritablePacket*)this->first(),tail(),count());
    } else {
        WritablePacket::recycle_packet_batch((WritablePacket*)this->first(),tail(),count());
    }
}
#endif

CLICK_ENDDECLS
#endif
