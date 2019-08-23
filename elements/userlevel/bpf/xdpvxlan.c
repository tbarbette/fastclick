// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2019 - The MergeTB Authors */
/*
 * derived from
 * https://github.com/netoptimizer/prototype-kernel/blob/master/kernel/samples/bpf/xdp_ddos01_blacklist_kern.c
 */

#define KBUILD_MODNAME "xdpvxlan"
#include <linux/bpf.h>
#include "bpf_helpers.h"
#include "common.h"

char _license[] SEC("license") = "GPL";

struct bpf_map_def SEC("maps") qidconf_map = {
  .type = BPF_MAP_TYPE_ARRAY,
  .key_size = sizeof(int),
  .value_size = sizeof(int),
  .max_entries = 1,
};

// TODO: do we really need a map in this case (1 entry only), or is there a 
// simpler way
struct bpf_map_def SEC("maps") xsk_map = {
  .type = BPF_MAP_TYPE_XSKMAP,
  .key_size = sizeof(int),
  .value_size = sizeof(int),
  .max_entries = 1,
};

SEC(KBUILD_MODNAME)
int xdpvlan_prog(struct xdp_md *ctx)
{
  // queue mapping
  int key = 0;
  int *qidconf = bpf_map_lookup_elem(&qidconf_map, &key);
  if (!qidconf) {
    return XDP_PASS;
  }

  if(*qidconf != ctx->rx_queue_index) {
    return XDP_PASS;
  }

  // packet handling 
	void *data_end = (void *)(long)ctx->data_end;
	void *data     = (void *)(long)ctx->data;
	struct ethhdr *eth = data;
	u16 eth_proto = 0;
	u64 l3_offset = 0;
	u32 action;

	if (!(parse_eth(eth, data_end, &eth_proto, &l3_offset))) {
		bpf_debug("Cannot parse L2: L3off:%llu proto:0x%x\n",
			  l3_offset, eth_proto);
		return XDP_PASS; /* Skip */
	}
	bpf_debug("Reached L3: L3off:%llu proto:0x%x\n", l3_offset, eth_proto);

	return handle_packet(&xsk_map, ctx, eth_proto, l3_offset);
}
