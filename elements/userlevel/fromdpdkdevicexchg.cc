// -*- c-basic-offset: 4; related-file-name: "fromdpdkdevice.hh" -*-
/*
 * fromdpdkdevice.{cc,hh} -- element reads packets live from network via
 * the DPDK.
 *
 * Copyright (c) 2014-2015 Cyril Soldani, University of Liège
 * Copyright (c) 2016-2017 Tom Barbette, University of Liège
 * Copyright (c) 2017 Georgios Katsikas, RISE SICS
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

#include "fromdpdkdevicexchg.hh"
extern "C" {
#include <rte_xchg.h>
}

CLICK_DECLS

FromDPDKDeviceXCHG::FromDPDKDeviceXCHG() {
}

FromDPDKDeviceXCHG::~FromDPDKDeviceXCHG() {}


/**
 *
 * This file implements a few buffering models. The real X-Change starts around line
 * 250, you probably want to jump there.
 */

#ifndef NOXCHG
//If NOXCHG is passed we don't compile any of this to prevent clashing wit rte_mbuf_xchg :)

# if XCHG_RX_SWAPONLY
/*This is an intermediate testing mode, where the application gives rte_mbuf*, and the driver (these functions) fill
 * the metadata of the mbuf. This allows to use only a few (32) in-flight metadata space.*/


    bool xchg_is_vec = true;

    inline struct rte_mbuf* get_buf(struct xchg* x) {
        return (struct rte_mbuf*)x;
    }

    CLICK_ALWAYS_INLINE void xchg_set_buffer(struct xchg* xchg, void* buf) {
        (void)xchg;
        (void)buf;
    }

    CLICK_ALWAYS_INLINE void xchg_set_packet_type(struct xchg* xchg, uint32_t ptype) {
        get_buf(xchg)->packet_type = ptype;
    }

    CLICK_ALWAYS_INLINE void xchg_set_rss_hash(struct xchg* xchg, uint32_t rss) {
        get_buf(xchg)->hash.rss = rss;
    }

    CLICK_ALWAYS_INLINE void xchg_set_timestamp(struct xchg* xchg, uint64_t t) {
        get_buf(xchg)->timestamp = t;
    }

    CLICK_ALWAYS_INLINE void xchg_set_flag(struct xchg* xchg, uint64_t f) {
        get_buf(xchg)->ol_flags |= f;
    }

    CLICK_ALWAYS_INLINE uint64_t xchg_get_flags(struct xchg* xchg) {
        return get_buf(xchg)->ol_flags;
    }

    CLICK_ALWAYS_INLINE uint16_t xchg_get_outer_l2_len(struct xchg* xchg) {
        return get_buf(xchg)->outer_l2_len;
    }

    CLICK_ALWAYS_INLINE uint16_t xchg_get_outer_l3_len(struct xchg* xchg) {
        return get_buf(xchg)->outer_l3_len;
    }

    CLICK_ALWAYS_INLINE void xchg_clear_flag(struct xchg* xchg, uint64_t f) {
        get_buf(xchg)->ol_flags &= f;
    }

    CLICK_ALWAYS_INLINE void xchg_set_fdir_id(struct xchg* xchg, uint32_t mark) {
        get_buf(xchg)->hash.fdir.hi = mark;
    }

    CLICK_ALWAYS_INLINE void xchg_set_vlan(struct xchg* xchg, uint32_t vlan) {
        get_buf(xchg)->vlan_tci = vlan;
    }

    CLICK_ALWAYS_INLINE uint32_t xchg_get_vlan(struct xchg* xchg) {
        return get_buf(xchg)->vlan_tci;
    }


    CLICK_ALWAYS_INLINE void xchg_set_len(struct xchg* xchg, uint16_t len) {
        rte_pktmbuf_pkt_len(get_buf(xchg)) = len;
    }
    CLICK_ALWAYS_INLINE void xchg_set_data_len(struct xchg* xchg, uint16_t len) {
       rte_pktmbuf_data_len(get_buf(xchg)) = len;

    }

    CLICK_ALWAYS_INLINE uint16_t xchg_get_len(struct xchg* xchg) {
        return rte_pktmbuf_pkt_len(get_buf(xchg));
    }

    CLICK_ALWAYS_INLINE void xchg_rx_finish_packet(struct xchg* xchg) {
        (void)xchg;
    }

    CLICK_ALWAYS_INLINE void xchg_rx_last_packet(struct xchg* xchg, struct xchg** xchgs) {
        (void)xchg;
        (void)xchgs;
    }

    /**
     * Take a packet from the ring and replace it by a new one
     */
    CLICK_ALWAYS_INLINE struct xchg* xchg_rx_next(struct rte_mbuf** pkt, struct xchg** xchgs, struct rte_mempool* mp) {
        (void) xchgs; //Mbuf is set on advance
        struct rte_mbuf* xchg = *pkt; //Buffer in the ring
		rte_prefetch0(xchg);
        *pkt = rte_mbuf_raw_alloc(mp); //Allocate packet to put back in the ring
        return (struct xchg*)xchg;
    }

    CLICK_ALWAYS_INLINE void xchg_rx_cancel(struct xchg* xchg, struct rte_mbuf* pkt) {
        (void)xchg;
        rte_mbuf_raw_free(pkt);
    }

    CLICK_ALWAYS_INLINE void xchg_rx_advance(struct xchg* xchg, struct xchg*** xchgs_p) {
        struct xchg** xchgs = *xchgs_p;
        *(xchgs++) = xchg; //Set in the user pointer the buffer from the ring
        *xchgs_p = xchgs;
    }

    CLICK_ALWAYS_INLINE void* xchg_buffer_from_elt(struct rte_mbuf* elt) {
        return rte_pktmbuf_mtod(elt, void*);
    }

# else

#  if CLICK_PACKET_INSIDE_DPDK
/*
 * This is an un-finished study to accelerate the "VPP" model, where the app buffer (in Click, the Packet object)
 * resides just after the rte_mbuf. X-Change allows to directly use the application buffer, so practically
 * X-Change is better. But we wanted to see how much X-Change could improve VPP.
 */

    bool xchg_is_vec = true;

    inline struct rte_pktmbuf* get_mbuf(struct xchg* x) {
        return (struct WritablePacket*)x;
    }

    inline WritablePacket* get_pkt(struct xchg* x) {
        return (struct WritablePacket*)(get_mbuf(x) + 1);
    }

    CLICK_ALWAYS_INLINE void xchg_set_packet_type(struct xchg* xchg, uint32_t ptype) {
        //get_buf(xchg)->packet_type = ptype;
    }

    CLICK_ALWAYS_INLINE void xchg_set_rss_hash(struct xchg* xchg, uint32_t rss) {
        SET_AGGREGATE_ANNO(get_pkt(xchg), rss);
    }

    CLICK_ALWAYS_INLINE void xchg_set_timestamp(struct xchg* xchg, uint64_t t) {
        get_pkt(xchg)->timestamp_anno().assignlong(t);
    }

    CLICK_ALWAYS_INLINE void xchg_set_flag(struct xchg* xchg, uint64_t f) {
//        get_buf(xchg)->ol_flags |= f;
    }

    CLICK_ALWAYS_INLINE void xchg_clear_flag(struct xchg* xchg, uint64_t f) {
    //    get_buf(xchg)->ol_flags &= f;
    }

    CLICK_ALWAYS_INLINE void xchg_set_fdir_id(struct xchg* xchg, uint32_t mark) {
        SET_AGGREGATE_ANNO(get_pkt(xchg), mark);
    }

    CLICK_ALWAYS_INLINE void xchg_set_vlan(struct xchg* xchg, uint32_t vlan) {
        SET_VLAN_TCI_ANNO(get_pkt(xchg),vlan);
    }

    CLICK_ALWAYS_INLINE void xchg_set_len(struct xchg* xchg, uint16_t len) {
        //get_buf(xchg)->set_buffer_length(len);
    }

    CLICK_ALWAYS_INLINE void xchg_set_data_len(struct xchg* xchg, uint16_t len) {
        get_pkt(xchg)->set_data_length(len);
    }

    CLICK_ALWAYS_INLINE uint16_t xchg_get_len(struct xchg* xchg) {
        return get_pkt(xchg)->buffer_length();
    }


    CLICK_ALWAYS_INLINE void xchg_rx_finish_packet(struct xchg* xchg) {
        (void)xchg;
    }

    CLICK_ALWAYS_INLINE void xchg_rx_last_packet(struct xchg* xchg, struct xchg** xchgs) {
        (void)xchg;
        (void)xchgs;
    }

    /**
     * Take a packet from the ring and replace it by a new one
     */
    CLICK_ALWAYS_INLINE struct xchg* xchg_rx_next(struct rte_mbuf** pkt, struct xchg** xchgs, struct rte_mempool* mp) {
        (void) xchgs; //Mbuf is set on advance
        struct rte_mbuf* xchg = *pkt; //Buffer in the ring
                rte_prefetch0(get_pkt(xchg));
        *pkt = rte_mbuf_raw_alloc(mp); //Allocate packet to put back in the ring
        return (struct xchg*)xchg;
    }

    CLICK_ALWAYS_INLINE void xchg_rx_cancel(struct xchg* xchg, struct rte_mbuf* pkt) {
        (void)xchg;
        rte_mbuf_raw_free(pkt);
    }

    CLICK_ALWAYS_INLINE void xchg_rx_advance(struct xchg* xchg, struct xchg*** xchgs_p) {
        struct xchg** xchgs = *xchgs_p;
        *(xchgs++) = xchg; //Set in the user pointer the buffer from the ring
        *xchgs_p = xchgs;
    }
    CLICK_ALWAYS_INLINE void* xchg_buffer_from_elt(struct rte_mbuf* elt) {
        return rte_pktmbuf_mtod(elt, void*);
    }

#  else
/**
 *
 * This is the real X-Change
 *
 * In (Fast)Click, the internal application buffer equivalent to the rte_mbuf
 * is the WritablePacket object. Hence, struct xchg* refers to a WritablePacket.
 * The goal of all the functions bellow is to fill the right field of WritablePacket,
 * eg xchg_set_vlan() will set the VLAN annotation of the WritablePacket to the
 * given value.
 */

    //In Click, we use LL and not vectors
    bool xchg_is_vec = false;

    /**
     * An internal helper to cast the xchg* pointer to the WritablePacket* pointer.
     */
    inline struct WritablePacket* get_buf(struct xchg* x) {
        return (struct WritablePacket*)x;
    }

    //Unused
    CLICK_ALWAYS_INLINE void xchg_set_packet_type(struct xchg* xchg, uint32_t ptype) {
        //get_buf(xchg)->packet_type = ptype; 
    }

    //The RSS hash is set in the AGGREGATE_ANNP
    CLICK_ALWAYS_INLINE void xchg_set_rss_hash(struct xchg* xchg, uint32_t rss) {
        SET_AGGREGATE_ANNO(get_buf(xchg), rss);
    }

    //Set the timestamp field
    CLICK_ALWAYS_INLINE void xchg_set_timestamp(struct xchg* xchg, uint64_t t) {
        get_buf(xchg)->timestamp_anno().assignlong(t);
    }

    //Unused
    CLICK_ALWAYS_INLINE void xchg_set_flag(struct xchg* xchg, uint64_t f) {
//        get_buf(xchg)->ol_flags |= f;
    }

    //Unused
    CLICK_ALWAYS_INLINE void xchg_clear_flag(struct xchg* xchg, uint64_t f) {
    //    get_buf(xchg)->ol_flags &= f;
    }

    //Fdir is also the aggregate, like RSS. One rarely use both (if needed, one can use another anno).
    CLICK_ALWAYS_INLINE void xchg_set_fdir_id(struct xchg* xchg, uint32_t mark) {
        SET_AGGREGATE_ANNO(get_buf(xchg), mark);
    }

    //Set the VLAN anno
    CLICK_ALWAYS_INLINE void xchg_set_vlan(struct xchg* xchg, uint32_t vlan) {
        SET_VLAN_TCI_ANNO(get_buf(xchg),vlan);
    }

    //Set packet length. However in our case the buffers themselves have a constant size, so no need to re-set
    CLICK_ALWAYS_INLINE void xchg_set_len(struct xchg* xchg, uint16_t len) {
        //get_buf(xchg)->set_buffer_length(len);
    }

    //Set data_length (the actual packet length)
    CLICK_ALWAYS_INLINE void xchg_set_data_len(struct xchg* xchg, uint16_t len) {
        get_buf(xchg)->set_data(get_buf(xchg)->buffer() + RTE_PKTMBUF_HEADROOM);
        get_buf(xchg)->set_data_length(len);
    }

    //Return the buffer length.
    CLICK_ALWAYS_INLINE uint16_t xchg_get_len(struct xchg* xchg) {
        return get_buf(xchg)->buffer_length();
    }

    //This functions is called "at the end of the for loop", when the driver has finished
    //with a packet, and will start reading the next one. It's a chance to wrap up what we
    //need to do.
    //In this case we prefetch the packet data, and set a few Click stuffs.
    CLICK_ALWAYS_INLINE void xchg_rx_finish_packet(struct xchg* xchg) {
        WritablePacket* p = (WritablePacket*)xchg;

        rte_prefetch0(p->data());

        p->set_packet_type_anno(Packet::HOST);
        p->set_mac_header(p->data());
        p->set_destructor_argument(p->buffer() - sizeof(rte_mbuf));
    }

    CLICK_ALWAYS_INLINE void xchg_rx_last_packet(struct xchg* xchg, struct xchg** xchgs) {
        *(((WritablePacket**)xchgs) - 1) = (WritablePacket*)xchg;
    }

    /**
     * This function is called by the driver to advance in the RX ring.
     * Set a new buffer to replace in the ring if not canceled, and return the next descriptor
     */
    CLICK_ALWAYS_INLINE struct xchg* xchg_rx_next(struct rte_mbuf** rep, struct xchg** xchgs, rte_mempool* mp) {
		//The user (actually, that's us) passes a linked-list of WritablePacket in the struct xchg** (so it's actually not a **)
        // moving to the next is actually taking the first packet. Note that advancing in the list is the
        // role of xchg_davance, not next. Next is like "peek".
        WritablePacket* first = (WritablePacket*)*xchgs;
        WritablePacket* p = first;

        //We'll take the address of the buffer and put that in the ring
        void* fresh_buf = (void*)first->buffer();

        //While the freshly received buffer with the new packet data
        unsigned char* buffer = ((unsigned char*)*rep) + sizeof(rte_mbuf);

        //We set the address in the ring
        *rep = (struct rte_mbuf*)(((unsigned char*)fresh_buf) - sizeof(struct rte_mbuf));

        //We set the address of the new buffer data in the Packet object
        first->set_buffer(buffer, DPDKDevice::MBUF_DATA_SIZE);

        return (struct xchg*)first;
    }

    /**
     * Cancel the current receiving, this should cancel the last xchg_rx_next.
     * It's how XCHG works, in the hope a receive will always work. I'm sure there are reasons for this.
     */
    CLICK_ALWAYS_INLINE void xchg_rx_cancel(struct xchg* xchg, struct rte_mbuf* rep) {
        WritablePacket* first = (WritablePacket*)xchg;
        first->set_buffer( ((unsigned char*)rep) + sizeof(rte_mbuf), DPDKDevice::MBUF_DATA_SIZE);
        first->set_data(((unsigned char*)rep) + sizeof(rte_mbuf) + RTE_PKTMBUF_HEADROOM);
    }

    /**
     * Pops the packet of the user provided buffers
     * @arg xchg is the packet being received (last call to xchg_next)
     */
    CLICK_ALWAYS_INLINE void xchg_rx_advance(struct xchg* xchg, struct xchg*** xchgs_p) {
        WritablePacket** xchgs = (WritablePacket**)*xchgs_p;
        WritablePacket* first = (WritablePacket*)xchg;
	    WritablePacket* next = (WritablePacket*)first->next();
    	*xchgs = next;
    }

    /**
     * Gives the buffer pointer from an mbuf in the driver's ring.
     * This implementation will not change. The idea was that one could avoid
     * Having rte_mbufs entierly, but it needs much more work in the driver...
     * Just for cleaniness.
     */
    CLICK_ALWAYS_INLINE void* xchg_buffer_from_elt(struct rte_mbuf* buf) {
        return ((unsigned char*)buf) + sizeof(rte_mbuf) + RTE_PKTMBUF_HEADROOM;
    }   
#  endif
# endif
#endif


bool FromDPDKDeviceXCHG::run_task(Task *t) {
  int ret = 0;

  int queue_idx = queue_for_thisthread_begin();

# if CLICK_PACKET_INSIDE_DPDK
  // The Packet object is just after rte_mbuf, the "VPP" mode
  struct rte_mbuf *pkts[_burst];
  unsigned n = rte_eth_rx_burst_xchg(_dev->port_id, queue_idx, pkts, _burst);
  if (n) {
    WritablePacket::pool_consumed_data_burst(n,tail[1]);
    add_count(n);
    ret = 1;
    PacketBatch* batch = (WritablePacket*)(pkts[0] + 1);
    WritablePacket* next = batch->first();
    WritablePacket* p;
    for (int i =0; i < n; i++) {
        p = next;
        rte_prefetch0(p->data());
        p->set_packet_type_anno(Packet::HOST);
        p->set_mac_header(p->data());
        next = (WritablePacket*)(pkts[i] + 1);
        p->set_next(next);
    }

    batch->make_tail(p,n);
    output_push_batch(0, batch);
  }

#elif HAVE_VECTOR_PACKET_POOL
  //Useless. With XCHG we can tell the driver how we advance, linked list, vector, or whatever.
  unsigned n = rte_eth_rx_burst_xchg(_dev->port_id, queue_idx, WritablePacket::pool_prepare_data(_burst), _burst);
  if (n) {
    WritablePacket::pool_consumed_data(n);
    add_count(n);
    ret = 1;
  }
  vect = PacketVector::alloc();
  vect->insert(burst, n);
  output_push_batch(0, vect);
#else
  //This is the real X-Change. No loop! Yeah :)
  WritablePacket* head = WritablePacket::pool_prepare_data_burst(_burst);
  WritablePacket* tail[2] = {0,head};
  unsigned n = rte_eth_rx_burst_xchg(_dev->port_id, queue_idx, (struct xchg**)&(tail[1]), _burst);
  if (n) {
    WritablePacket::pool_consumed_data_burst(n,(WritablePacket*)(tail[0]->next()));
    add_count(n);
    ret = 1;
    PacketBatch* batch = PacketBatch::make_from_simple_list(head,tail[0],n);
    output_push_batch(0, batch);
  }
#endif

  /*We reschedule directly, as we cannot know if there is actually packet
   * available and dpdk has no select mechanism*/
  t->fast_reschedule();
  return (ret);
}


CLICK_ENDDECLS

ELEMENT_REQUIRES(FromDPDKDevice !dpdk-packet dpdk-xchg)
EXPORT_ELEMENT(FromDPDKDeviceXCHG)
ELEMENT_MT_SAFE(FromDPDKDeviceXCHG)
