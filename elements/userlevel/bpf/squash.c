// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2019 - The MergeTB Authors */

#define KBUILD_MODNAME "moaq"
#include <linux/bpf.h>
#include "bpf_helpers.h"
#include "common.h"

char _license[] SEC("license") = "GPL";

struct bpf_map_def SEC("maps") xsks_map = {
  .type = BPF_MAP_TYPE_XSKMAP,
  .key_size = sizeof(int),
  .value_size = sizeof(int),
  .max_entries = 64,
};

SEC("xdp_sock")
int moaq_prog(struct xdp_md *ctx)
{
  bpf_debug("pkt: @%d:%d\n", ctx->ingress_ifindex, ctx->rx_queue_index);

  int index = 0;

  if (bpf_map_lookup_elem(&xsks_map, &index)) {
    bpf_debug("squashing %d -> %d\n", ctx->rx_queue_index, index);
    return bpf_redirect_map(
        &xsks_map, 
        index, // key into the xdp socket map
        0  // no special flags
    );
  }
  return XDP_PASS;

}
