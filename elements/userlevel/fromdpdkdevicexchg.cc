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

    __thread  WritablePacket* last;
#ifndef NOXCHG

/**
 * Free the mbufs from the linear array of pointers.
 *
 * @param pkts
 *   Pointer to array of packets to be free.
 * @param pkts_n
 *   Number of packets to be freed.
 * @param olx
 *   Configured Tx offloads mask. It is fully defined at
 *   compile time and may be used for optimization.
 */
static __rte_always_inline void
mlx5_tx_free_mbuf(struct rte_mbuf ** pkts,
		  unsigned int pkts_n,
		  unsigned int olx __rte_unused)
{
	struct rte_mempool *pool = NULL;
	struct rte_mbuf **p_free = NULL;
	struct rte_mbuf *mbuf;
	unsigned int n_free = 0;

	/*
	 * The implemented algorithm eliminates
	 * copying pointers to temporary array
	 * for rte_mempool_put_bulk() calls.
	 */
	for (;;) {
		for (;;) {
			/*
			 * Decrement mbuf reference counter, detach
			 * indirect and external buffers if needed.
			 */
			mbuf = rte_pktmbuf_prefree_seg(*pkts);
			if (likely(mbuf != NULL)) {
				if (likely(n_free != 0)) {
					if (unlikely(pool != mbuf->pool))
						/* From different pool. */
						break;
				} else {
					/* Start new scan array. */
					pool = mbuf->pool;
					p_free = pkts;
				}
				++n_free;
				++pkts;
				--pkts_n;
				if (unlikely(pkts_n == 0)) {
					mbuf = NULL;
					break;
				}
			} else {
				/*
				 * This happens if mbuf is still referenced.
				 * We can't put it back to the pool, skip.
				 */
				++pkts;
				--pkts_n;
				if (unlikely(n_free != 0))
					/* There is some array to free.*/
					break;
				if (unlikely(pkts_n == 0))
					/* Last mbuf, nothing to free. */
					return;
			}
		}
		for (;;) {
			/*
			 * This loop is implemented to avoid multiple
			 * inlining of rte_mempool_put_bulk().
			 */
			/*
			 * Free the array of pre-freed mbufs
			 * belonging to the same memory pool.
			 */
			rte_mempool_put_bulk(pool, (void * const*)p_free, n_free);
			if (unlikely(mbuf != NULL)) {
				/* There is the request to start new scan. */
				pool = mbuf->pool;
				p_free = pkts++;
				n_free = 1;
				--pkts_n;
				if (likely(pkts_n != 0))
					break;
				/*
				 * This is the last mbuf to be freed.
				 * Do one more loop iteration to complete.
				 * This is rare case of the last unique mbuf.
				 */
				mbuf = NULL;
				continue;
			}
			if (likely(pkts_n == 0))
				return;
			n_free = 0;
			break;
		}
	}
}

# if XCHG_RX_SWAPONLY

    inline struct rte_mbuf* get_buf(struct xchg* x) {
        return (struct rte_mbuf*)x;
    }

    void xchg_set_buffer(struct xchg* xchg, void* buf) {
        (void)xchg;
        (void)buf;
    }

    void xchg_set_packet_type(struct xchg* xchg, uint32_t ptype) {
        get_buf(xchg)->packet_type = ptype;
    }

    void xchg_set_rss_hash(struct xchg* xchg, uint32_t rss) {
        get_buf(xchg)->hash.rss = rss;
    }

    void xchg_set_timestamp(struct xchg* xchg, uint64_t t) {
        get_buf(xchg)->timestamp = t;
    }

    void xchg_set_flag(struct xchg* xchg, uint64_t f) {
        get_buf(xchg)->ol_flags |= f;
    }

    uint64_t xchg_get_flags(struct xchg* xchg) {
        return get_buf(xchg)->ol_flags;
    }

    uint16_t xchg_get_outer_l2_len(struct xchg* xchg) {
        return get_buf(xchg)->outer_l2_len;
    }

    uint16_t xchg_get_outer_l3_len(struct xchg* xchg) {
        return get_buf(xchg)->outer_l3_len;
    }

// int xchg_has_flag(struct xchg* xchg, uint64_t f) {
//       return get_buf(xchg)->ol_flags & f;
//    }

    void xchg_clear_flag(struct xchg* xchg, uint64_t f) {
        get_buf(xchg)->ol_flags &= f;
    }

    void xchg_set_fdir_id(struct xchg* xchg, uint32_t mark) {
        get_buf(xchg)->hash.fdir.hi = mark;
    }

    void xchg_set_vlan(struct xchg* xchg, uint32_t vlan) {
        get_buf(xchg)->vlan_tci = vlan;
    }

    uint32_t xchg_get_vlan(struct xchg* xchg) {
        return get_buf(xchg)->vlan_tci;
    }


    void xchg_set_len(struct xchg* xchg, uint16_t len) {
        rte_pktmbuf_pkt_len(get_buf(xchg)) = len;
    }
    void xchg_set_data_len(struct xchg* xchg, uint16_t len) {
       rte_pktmbuf_data_len(get_buf(xchg)) = len;

    }

    uint16_t xchg_get_len(struct xchg* xchg) {
        return rte_pktmbuf_pkt_len(get_buf(xchg));
    }

    void xchg_finish_packet(struct xchg* xchg) {
        (void)xchg;
    }

    /**
     * Take a packet from the ring and replace it by a new one
     */
    struct xchg* xchg_next(struct rte_mbuf** pkt, struct xchg** xchgs, struct rte_mempool* mp) {
        (void) xchgs; //Mbuf is set on advance
        struct rte_mbuf* xchg = *pkt; //Buffer in the ring
		rte_prefetch0(xchg);
        *pkt = rte_mbuf_raw_alloc(mp); //Allocate packet to put back in the ring
        return (struct xchg*)xchg;
    }

    void xchg_cancel(struct xchg* xchg, struct rte_mbuf* pkt) {
        (void)xchg;
        rte_mbuf_raw_free(pkt);
    }

    void xchg_advance(struct xchg* xchg, struct xchg*** xchgs_p) {
        struct xchg** xchgs = *xchgs_p;
        *(xchgs++) = xchg; //Set in the user pointer the buffer from the ring
        *xchgs_p = xchgs;
    }
    void* xchg_buffer_from_elt(struct rte_mbuf* elt) {
        return rte_pktmbuf_mtod(elt, void*);
    }
# else

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

# endif


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
  unsigned n = rte_mlx5_rx_burst_xchg(_dev->port_id, iqueue, (struct xchg**)&tail, _burst);
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

ELEMENT_REQUIRES(FromDPDKDevice !dpdk-packet dpdk-xchg)
EXPORT_ELEMENT(FromDPDKDeviceXCHG)
ELEMENT_MT_SAFE(FromDPDKDeviceXCHG)
