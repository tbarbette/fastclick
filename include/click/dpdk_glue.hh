#ifndef DPDKGLUE_H
#define DPDKGLUE_H
#include <rte_mbuf.h>
#include <rte_version.h>
#include <rte_hash_crc.h>
#include <click/ipflowid.hh>

#if RTE_VERSION >= RTE_VERSION_NUM(22,07,0,0)
#define PKT_RX_TIMESTAMP RTE_MBUF_F_RX_TIMESTAMP
#endif

inline uint32_t
ipv4_hash_crc(const void *data,  uint32_t data_len,
                uint32_t init_val)
{
    (void)data_len;
    const IPFlow5ID *k;
    uint32_t t;
    const uint32_t *p;
    k = (const IPFlow5ID *)data;
    t = k->proto();
    p = ((const uint32_t *)k) + 2;
    init_val = rte_hash_crc_4byte(t, init_val);
    init_val = rte_hash_crc_4byte(k->saddr(), init_val);
    init_val = rte_hash_crc_4byte(k->daddr(), init_val);
    init_val = rte_hash_crc_4byte(*p, init_val);
    return init_val;
}

#if RTE_VERSION <= RTE_VERSION_NUM(2,2,0,0)
static inline int rte_ring_mc_dequeue_bulk(struct rte_ring *r, void **obj_table, unsigned n, void *) {
    rte_ring_mc_dequeue_bulk(r, obj_table,n);
}
#endif

#ifndef RTE_MBUF_INDIRECT
#define RTE_MBUF_INDIRECT(m) RTE_MBUF_CLONED(m)
#endif

#if RTE_VERSION < RTE_VERSION_NUM(20,11,0,0)
#define TIMESTAMP_FIELD(mbuf) \
            (mbuf->timestamp)
#define HAS_TIMESTAMP(mbuf) \
        (mbuf->ol_flags & PKT_RX_TIMESTAMP)
#else
extern "C" {
#include <rte_mbuf_dyn.h>
}
#include <rte_bitops.h>
#define TIMESTAMP_FIELD(mbuf) \
           (*RTE_MBUF_DYNFIELD(mbuf, timestamp_dynfield_offset, uint64_t *))
#define HAS_TIMESTAMP(mbuf) \
        ((mbuf)->ol_flags & timestamp_dynflag)

extern int timestamp_dynfield_offset;
extern uint64_t timestamp_dynflag;
#endif


#endif
