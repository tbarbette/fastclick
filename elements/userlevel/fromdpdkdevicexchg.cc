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
#ifdef DPDK_USE_XCHG
extern "C" {
#include <mlx5_xchg.h>
}
#endif

CLICK_DECLS

FromDPDKDeviceXCHG::FromDPDKDeviceXCHG() {
}

FromDPDKDeviceXCHG::~FromDPDKDeviceXCHG() {}

    __thread  WritablePacket* last;
#ifndef NOXCHG
    inline struct WritablePacket* get_buf(struct xchg* x) {
        return (struct WritablePacket*)x;
    }

    void xchg_set_packet_type(struct xchg* xchg, uint32_t ptype) {
        //get_buf(xchg)->packet_type = ptype; 
    }

    void xchg_set_rss_hash(struct xchg* xchg, uint32_t rss) {
        SET_AGGREGATE_ANNO(get_buf(xchg), rss);
    }

    void xchg_set_timestamp(struct xchg* xchg, uint64_t t) {
        get_buf(xchg)->timestamp_anno().assignlong(t);
    }

    void xchg_set_flag(struct xchg* xchg, uint64_t f) {
//        get_buf(xchg)->ol_flags |= f;
    }

    void xchg_clear_flag(struct xchg* xchg, uint64_t f) {
    //    get_buf(xchg)->ol_flags &= f;
    }

    void xchg_set_fdir_id(struct xchg* xchg, uint32_t mark) {
        SET_AGGREGATE_ANNO(get_buf(xchg), mark);
    }

    void xchg_set_vlan(struct xchg* xchg, uint32_t vlan) {
        SET_VLAN_TCI_ANNO(get_buf(xchg),vlan);
    }

    void xchg_set_len(struct xchg* xchg, uint16_t len) {
        //get_buf(xchg)->set_buffer_length(len);
    }

    void xchg_set_data_len(struct xchg* xchg, uint16_t len) {
        get_buf(xchg)->set_data_length(len);
    }

    uint16_t xchg_get_len(struct xchg* xchg) {
        return get_buf(xchg)->buffer_length();
    }


    void xchg_finish_packet(struct xchg* xchg) {
        WritablePacket* p = (WritablePacket*)xchg;

        //click_chatter("Packet %p, head %p, data %p, tail %p, end %p", p, p->buffer(), p->data(), p->end_data(), p->end_buffer());
        rte_prefetch0(p->data());
        p->set_packet_type_anno(Packet::HOST);
        p->set_mac_header(p->data());
        p->set_destructor_argument(p->buffer() - RTE_PKTMBUF_HEADROOM);
/*        if (_set_rss_aggregate)
#if RTE_VERSION > RTE_VERSION_NUM(1, 7, 0, 0)
          SET_AGGREGATE_ANNO(p, pkts[i]->hash.rss);
#else
          SET_AGGREGATE_ANNO(p, pkts[i]->pkt.hash.rss);
#endif
        if (_set_paint_anno) {
          SET_PAINT_ANNO(p, iqueue);
        }*/

        last = p;
    }

    /**
     * Set a new buffer to replace in the ring if not canceled, and return the next descriptor
     */
    struct xchg* xchg_next(struct rte_mbuf** rep, struct xchg** xchgs, rte_mempool* mp) {
		WritablePacket* first = (WritablePacket*)*xchgs;

        void* fresh_buf = (void*)first->buffer();

        unsigned char* buffer = ((unsigned char*)*rep) + sizeof(rte_mbuf);
        *rep = (struct rte_mbuf*)(((unsigned char*)fresh_buf) - sizeof(struct rte_mbuf));

        first->set_buffer(buffer, DPDKDevice::MBUF_DATA_SIZE);
//        assert(buffer + RTE_PKTMBUF_HEADROOM == rte_pktmbuf_mtod(*elts, unsigned char*));
//        click_chatter("BUF %p, ARG %p, CALC %p",fresh_buf, first->destructor_argument(), *elts);
//        assert(*elts == first->destructor_argument());
        return (struct xchg*)first;
    }

    void xchg_cancel(struct xchg* xchg, struct rte_mbuf* rep) {
        WritablePacket* first = (WritablePacket*)xchg;
        first->set_buffer( ((unsigned char*)rep) + sizeof(rte_mbuf), DPDKDevice::MBUF_DATA_SIZE);
    }

    void xchg_advance(struct xchg* xchg, struct xchg*** xchgs_p) {
        WritablePacket** xchgs = (WritablePacket**)*xchgs_p;
        WritablePacket* first = (WritablePacket*)xchg;
	    WritablePacket* next = (WritablePacket*)first->next();
    	*xchgs = next;
        first->set_data(first->buffer() + RTE_PKTMBUF_HEADROOM);
    }
    
    void* xchg_buffer_from_elt(struct rte_mbuf* buf) {
        return ((unsigned char*)buf) + sizeof(rte_mbuf) + RTE_PKTMBUF_HEADROOM;
    }   

#endif
    /*    void* xchg_fill_elts() {
        struct rte_mbuf* = DPDKDevice::allocate();
    }*/

bool FromDPDKDeviceXCHG::run_task(Task *t) {
  struct rte_mbuf *pkts[_burst];
  int ret = 0;

  int iqueue = queue_for_thisthread_begin();

//  unsigned n = rte_eth_rx_burst(_dev->port_id, iqueue, pkts, _burst);
  
    //unsigned n = rte_mlx5_rx_burst_stripped(_dev->port_id, iqueue, pkts, _burst);

//    struct xchg** xchgs = (struct xchg**)pkts;

//    unsigned n = rte_mlx5_rx_burst_xchg(_dev->port_id, iqueue, xchgs, _burst);
//
#if HAVE_VECTOR_PACKET_POOL
  unsigned n = rte_mlx5_rx_burst_xchg(_dev->port_id, iqueue, WritablePacket::pool_prepare_data(_burst), _burst);
  if (n) {
    WritablePacket::pool_consumed_data(n);
    add_count(n);
    ret = 1;
  }
  vect = PacketVector::alloc();
  vect->insert(burst, n);
  output_push_batch(0, vect);
#else
  WritablePacket* head = WritablePacket::pool_prepare_data_burst(_burst);
  WritablePacket* tail = head;
#ifdef DPDK_USE_XCHG
  unsigned n = rte_mlx5_rx_burst_xchg(_dev->port_id, iqueue, (struct xchg**)&tail, _burst);
#else
  unsigned n = 0;
  assert(false);
#endif
  if (n) {
    WritablePacket::pool_consumed_data_burst(n,tail);
    add_count(n);
    ret = 1;
    PacketBatch* batch = PacketBatch::make_from_simple_list(head,last,n);
    output_push_batch(0, batch);
  }
#endif

  /*We reschedule directly, as we cannot know if there is actually packet
   * available and dpdk has no select mechanism*/
  t->fast_reschedule();
  return (ret);
}


CLICK_ENDDECLS

ELEMENT_REQUIRES(FromDPDKDevice)
EXPORT_ELEMENT(FromDPDKDeviceXCHG)
ELEMENT_MT_SAFE(FromDPDKDeviceXCHG)
