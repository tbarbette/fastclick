// -*- related-file-name: "../include/click/packetbatch.hh" -*-
/*
 * packetbatch.{cc,hh} -- a batch of packet
 * Tom Barbette
 *
 * Copyright (c) 2015-2016 University of Liege
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
#include <click/packetbatch.hh>

CLICK_DECLS

#if HAVE_BATCH

# if HAVE_CLICK_PACKET_POOL
/**
 * Recycle a whole batch of unshared packets of the same type
 *
 * @precond No packet are shared
 */
void PacketBatch::safe_kill(bool is_data) {
    if (is_data) {
        WritablePacket::recycle_data_batch(this,tail(),count());
    } else {
        WritablePacket::recycle_packet_batch(this,tail(),count());
    }
}

/**
 * Recycle a whole batch, faster in most cases
 */
void PacketBatch::fast_kill() {
    BATCH_RECYCLE_START();
    FOR_EACH_PACKET_SAFE(this,up) {
        WritablePacket* p = static_cast<WritablePacket*>(up);
        BATCH_RECYCLE_UNSAFE_PACKET(p);
    }
    BATCH_RECYCLE_END();
}
# endif

/** @brief Create and return a batch of packets made from a contiguous buffer
 * @param count number of packets
 *
 * @param data data used in the new packet
 * @param length array of packets length
 * @param destructor destructor function
 * @param argument argument to destructor function
 * @return new packet batch, or null if no packet could be created
 *
 **/
PacketBatch *
PacketBatch::make_batch(unsigned char *data, uint16_t count, uint16_t *length,
        buffer_destructor_type destructor, void* argument )
{
#if CLICK_PACKET_USE_DPDK
assert(false); //TODO
#endif

# if HAVE_CLICK_PACKET_POOL
    WritablePacket *p = WritablePacket::pool_batch_allocate(count);
# else
    WritablePacket *p = new WritablePacket;
# endif
    WritablePacket *head = p;
    WritablePacket *last = p;
    uint16_t i = 0;
    while(p) {
        p->initialize();
        p->_head = p->_data = data;
        p->_tail = p->_end = data + length[i];
        data += length[i] & 63 ? (length[i] & ~63) + 64 : length[i];
        ++i;
        p->_destructor = destructor;
        p->_destructor_argument = argument;
        last = p;
#if HAVE_CLICK_PACKET_POOL
        p = static_cast<WritablePacket *>(p->next());
#else
        WritablePacket *p = new WritablePacket;
#endif
    }
    if (i != count) {
        click_chatter("Size of list %d, expected %d\n", i, count);
    }
    return PacketBatch::make_from_simple_list(head, last, i);
}


# if HAVE_NETMAP_PACKET_POOL
/**
 * Creates a batch of packet directly from a netmap ring.
 */
PacketBatch* WritablePacket::make_netmap_batch(unsigned int n, struct netmap_ring* rxring,unsigned int &cur) {
    if (n <= 0) return NULL;
    struct netmap_slot* slot;
    WritablePacket* last = 0;

    PacketPool& packet_pool = *make_local_packet_pool();

    WritablePacket*& _head = packet_pool.pd;
    unsigned int & _count = packet_pool.pdcount;

    if (_count == 0) {
        _head = pool_data_allocate();
        _count = 1;
    }

    //Next is the current packet in the batch
    Packet* next = _head;
    //P_batch_saved is the saved head of the batch.
    PacketBatch* p_batch = static_cast<PacketBatch*>(_head);

    int toreceive = n;
    while (toreceive > 0) {

        last = static_cast<WritablePacket*>(next);

        slot = &rxring->slot[cur];

        unsigned char* data = (unsigned char*)NETMAP_BUF(rxring, slot->buf_idx);
        __builtin_prefetch(data);

        slot->buf_idx = NETMAP_BUF_IDX(rxring,last->buffer());

        slot->flags = NS_BUF_CHANGED;

        next = last->next(); //Correct only if count != 0
        last->initialize();

        last->set_buffer(data,NetmapBufQ::buffer_size(),slot->len);
        cur = nm_ring_next(rxring, cur);
        toreceive--;
        _count --;

        if (_count == 0) {

            _head = 0;

            next = pool_data_allocate();
            _count++; // We use the packet already out of the pool
        }
        last->set_next(next);

    }

    _head = static_cast<WritablePacket*>(next);

    p_batch->set_count(n);
    p_batch->set_tail(last);
    last->set_next(NULL);
    return p_batch;
}
# endif //HAVE_NETMAP_PACKET_POOL

#endif //HAVE_BATCH

CLICK_ENDDECLS
