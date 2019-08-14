// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2019 - The MergeTB Authors */

#define KBUILD_MODNAME "moaq"
#include <linux/bpf.h>
#include "bpf_helpers.h"

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

SEC("moaq")
int moaq_prog(struct xdp_md *ctx)
{
  int key = 0;
  int *qidconf = bpf_map_lookup_elem(&qidconf_map, &key);
  if (!qidconf) {
    return XDP_PASS;
    //return XDP_ABORTED;
  }

  if(*qidconf != ctx->rx_queue_index) {
    return XDP_PASS;
  }

  // redirect all packets on the interface to an XDP socket
  return bpf_redirect_map(
      &xsk_map, 
      0, // key into the xdp socket map, for this app there ins only 1 socket
      0  // no special flags
  );
}
