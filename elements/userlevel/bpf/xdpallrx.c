// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

#define MAX_SOCKS 4

#define bpf_debug(fmt, ...)				                \
        ({                                                              \
        char ____fmt[] = fmt;				                \
        bpf_trace_printk(____fmt, sizeof(____fmt), ##__VA_ARGS__);      \
        })

char _license[] SEC("license") = "GPL";

struct {
        __uint(type, BPF_MAP_TYPE_XSKMAP);
        __uint(max_entries, MAX_SOCKS);
        __uint(key_size, sizeof(int));
        __uint(value_size, sizeof(int));
} xsks_map SEC(".maps");

static unsigned int index;

SEC("xdp_sock") int xdp_sock_prog(struct xdp_md *ctx)
{
        //bpf_debug("pkt: @%d:%d\n", ctx->ingress_ifindex, ctx->rx_queue_index);

        return bpf_redirect_map(&xsks_map, ctx->rx_queue_index, XDP_DROP);
}
