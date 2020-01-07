#ifndef DPDKGLUE_H
#define DPDKGLUE_H
#include <rte_version.h>
#include <rte_hash_crc.h>
#include <click/ipflowid.hh>

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

#endif
