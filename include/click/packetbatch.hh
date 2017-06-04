// -*- related-file-name: "../../lib/packetbatch.cc" -*-
#ifndef CLICK_PACKETBATCH_HH
#define CLICK_PACKETBATCH_HH
#if HAVE_DPDK_PACKET_POOL
# include <click/dpdkdevice.hh>
#endif
#include <click/packet.hh>
CLICK_DECLS

#define FOR_EACH_PACKET(batch,p) for(Packet* p = batch;p != NULL;p=p->next())

#define FOR_EACH_PACKET_SAFE(batch,p) \
                Packet* next = ((batch != NULL)? batch->next() : NULL );\
                Packet* p = batch;\
                for (;p != NULL;p=next,next=(p==0?0:p->next()))

/**
 * Execute a function on each packets of a batch. The function may return
 * another packet to replace the current one. This version cannot drop !
 * Use _DROPPABLE version if the function could return null.
 */
#define EXECUTE_FOR_EACH_PACKET(fnt,batch) \
                Packet* next = ((batch != NULL)? batch->next() : NULL );\
                Packet* p = batch;\
                Packet* last = NULL;\
                for (;p != NULL;p=next,next=(p==0?0:p->next())) {\
            Packet* q = fnt(p);\
                    if (q != p) {\
                        if (last) {\
                            last->set_next(q);\
                        } else {\
                            batch = static_cast<PacketBatch*>(q);\
                        }\
                        q->set_next(next);\
                    }\
                    last = q;\
                }

/**
 * Execute a function on each packet of a batch. The function may return
 * another packet, or null if the packet could be dropped.
 */
#define EXECUTE_FOR_EACH_PACKET_DROPPABLE(fnt,batch,on_drop) {\
                Packet* next = ((batch != NULL)? batch->next() : NULL );\
                Packet* p = batch;\
                Packet* last = NULL;\
                int count = batch->count();\
                for (;p != NULL;p=next,next=(p==0?0:p->next())) {\
            Packet* q = fnt(p);\
            if (q == 0) {\
                on_drop(p);\
                if (last) {\
                    last->set_next(next);\
                } else {\
                    batch = PacketBatch::start_head(next);\
                }\
                        count--;\
                        continue;\
            } else if (q != p) {\
                        if (last) {\
                            last->set_next(q);\
                        } else {\
                            batch = static_cast<PacketBatch*>(q);\
                        }\
                        q->set_next(next);\
                    }\
                    last = q;\
                }\
                if (batch) {\
                    batch->set_count(count);\
                    batch->set_tail(last);\
                }\
            }\

/**
 * Split a batch into multiple batch according to a given function which will
 * give the index of an output to choose.
 * @fnt Function to call which will return a value between 0 and nbatches
 * #on_finish function which take an output index and the batch when classification is finished
 */
#define CLASSIFY_EACH_PACKET(nbatches,fnt,batch,on_finish)\
    {\
        PacketBatch* out[nbatches];\
        bzero(out,sizeof(PacketBatch*)*nbatches);\
        PacketBatch* next = ((batch != NULL)? static_cast<PacketBatch*>(batch->next()) : NULL );\
        PacketBatch* p = batch;\
        PacketBatch* last = NULL;\
        int last_o = -1;\
        int passed = 0;\
        for (;p != NULL;p=next,next=(p==0?0:static_cast<PacketBatch*>(p->next()))) {\
            int o = (fnt(p));\
            if (o < 0 || o>=(nbatches)) o = (nbatches - 1);\
            if (o == last_o) {\
                passed ++;\
            } else {\
                if (last == NULL) {\
                    out[o] = p;\
                    p->set_count(1);\
                    p->set_tail(p);\
                } else {\
                    out[last_o]->set_tail(last);\
                    out[last_o]->set_count(out[last_o]->count() + passed);\
                    if (!out[o]) {\
                        out[o] = p;\
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
        int i = 0;\
        for (; i < nbatches; i++) {\
            if (out[i]) {\
                out[i]->tail()->set_next(NULL);\
                (on_finish(i,out[i]));\
            }\
        }\
    }

/**
 * Create a batch by calling multiple times (up to max) a given function
 */
#define MAKE_BATCH(fnt,head,max) {\
        head = PacketBatch::start_head(fnt);\
        Packet* last = head;\
        if (head != NULL) {\
            unsigned int count = 1;\
            while (count < (max>0?max:BATCH_MAX_PULL)) {\
                Packet* current = fnt;\
                if (current == NULL)\
                    break;\
                last->set_next(current);\
                last = current;\
                count++;\
            }\
            head->make_tail(last,count);\
        } else head = NULL;\
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
class PacketBatch : public WritablePacket {

//Consider a batch size bigger as bogus (prevent infinite loop on bad pointer manipulation)
#define MAX_BATCH_SIZE 8192

public :

    inline void set_tail(Packet* p) {
        set_prev(p);
    }

    inline Packet* tail() {
        return prev();
    }

    inline void append_batch(PacketBatch* head) {
        tail()->set_next(head);
        set_tail(head->tail());
        set_count(count() + head->count());
    }

    inline void append_packet(Packet* p) {
        tail()->set_next(p);
        set_tail(p);
        set_count(count() + 1);
    }

    /**
     * Return the number of packets in this batch
     */
    inline unsigned count() {
        unsigned int r = BATCH_COUNT_ANNO(this);
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
     */
    inline static PacketBatch* start_head(Packet* p) {
        return static_cast<PacketBatch*>(p);
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
        if (last == NULL) {
            if (count != 1)
                click_chatter("BUG in make_tail : last packet is the head, but count is %u",count);
            set_tail(this);
            set_next(NULL);
        } else {
            set_tail(last);
            last->set_next(NULL);
        }
        return this;
    }

    /**
     * Set the number of packets in this batch
     */
    inline void set_count(unsigned int c) {
        SET_BATCH_COUNT_ANNO(this,c);
    }

    /**
     * @brief Cut a batch in two batches
     *
     * @param middle The last packet of the first batch
     * @param first_batch_count The number of packets in the first batch
     * @param second Reference to set the head of the second batch
     */
    inline void cut(Packet* middle, int first_batch_count, PacketBatch* &second) {
        if (middle == NULL) {
            second = NULL;
            click_chatter("BUG Warning : cutting a batch without a location to cut !");
            return;
        }

        if (middle == tail()) {
            second = NULL;
            return;
        }

        int total_count = count();

        second = static_cast<PacketBatch*>(middle->next());
        middle->set_next(NULL);

        Packet* second_tail = tail();
        set_tail(middle);

        second->set_tail(second_tail);
        second->set_count(total_count - first_batch_count);

        set_count(first_batch_count);
    }

    /**
     * Build a batch from a linked list of packet
     *
     * @param head The first packet of the batch
     * @param size Number of packets in the linkedlist
     *
     * The "prev" annotation of the first packet must point to the last packet of the linked list
     */
    inline static PacketBatch* make_from_list(Packet* head, unsigned int size) {
        PacketBatch* b = static_cast<PacketBatch*>(head);
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
        PacketBatch* b = make_from_list(head,size);
        b->set_tail(tail);
        tail->set_next(0);
        return b;
    }

    /**
     * Make a batch composed of a single packet
     */
    inline static PacketBatch* make_from_packet(Packet* p) {
        if (!p) return 0;
        PacketBatch* b =  static_cast<PacketBatch*>(p);
        b->set_count(1);
        b->set_tail(b);
        b->set_next(0);
        return b;
    }

    static PacketBatch *make_batch(unsigned char *data, uint16_t count, uint16_t *length,
                    buffer_destructor_type destructor,
                                    void* argument = (void*) 0) CLICK_WARN_UNUSED_RESULT;

    /**
     * Return the first packet of this batch
     */
    inline Packet* begin() {
        return this;
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
        FOR_EACH_PACKET(this,p) {
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
    void safe_kill(bool is_data);

    void fast_kill();
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
	WritablePacket* head_packet = NULL;\
	WritablePacket* head_data = NULL;\
	WritablePacket* last_packet = NULL;\
	WritablePacket* last_data = NULL;\
	unsigned int n_packet = 0;\
	unsigned int n_data = 0;

#define BATCH_RECYCLE_PACKET(p) {\
	if (head_packet == NULL) {\
		head_packet = static_cast<WritablePacket*>(p);\
		last_packet = static_cast<WritablePacket*>(p);\
	} else {\
		last_packet->set_next(p);\
		last_packet = static_cast<WritablePacket*>(p);\
	}\
	n_packet++;}

#define BATCH_RECYCLE_DATA_PACKET(p) {\
	if (head_data == NULL) {\
		head_data = static_cast<WritablePacket*>(p);\
		last_data = static_cast<WritablePacket*>(p);\
	} else {\
		last_data->set_next(p);\
		last_data = static_cast<WritablePacket*>(p);\
	}\
	n_data++;}

#define BATCH_RECYCLE_UNSAFE_PACKET(p) {\
			if (p->shared()) {\
				p->kill();\
			} else {\
				BATCH_RECYCLE_UNKNOWN_PACKET(p);\
			}\
		}

#if HAVE_DPDK_PACKET_POOL
#define BATCH_RECYCLE_UNKNOWN_PACKET(p) {\
	if (p->data_packet() == 0 && p->buffer_destructor() == DPDKDevice::free_pkt && p->buffer() != 0) {\
		BATCH_RECYCLE_DATA_PACKET(p);\
	} else {\
		BATCH_RECYCLE_PACKET(p);}}
#else
#define BATCH_RECYCLE_UNKNOWN_PACKET(p) {\
	if (p->data_packet() == 0 && p->buffer_destructor() == 0 && p->buffer() != 0) {\
		BATCH_RECYCLE_DATA_PACKET(p);\
	} else {\
		BATCH_RECYCLE_PACKET(p);}}
#endif

#define BATCH_RECYCLE_END() \
	if (last_packet) {\
		last_packet->set_next(0);\
		PacketBatch::make_from_simple_list(head_packet,last_packet,n_packet)->safe_kill(false);\
	}\
	if (last_data) {\
		last_data->set_next(0);\
		PacketBatch::make_from_simple_list(head_data,last_data,n_data)->safe_kill(true);\
	}
#else
#define BATCH_RECYCLE_START() {}
#define BATCH_RECYCLE_END() {}
#define BATCH_RECYCLE_UNSAFE_PACKET(p) {p->kill();}
#endif

typedef Packet::PacketType PacketType;

CLICK_ENDDECLS
#endif
