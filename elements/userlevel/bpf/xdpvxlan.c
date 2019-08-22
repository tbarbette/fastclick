// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2019 - The MergeTB Authors */
/*
 * derived from
 * https://github.com/netoptimizer/prototype-kernel/blob/master/kernel/samples/bpf/xdp_ddos01_blacklist_kern.c
 */

#define KBUILD_MODNAME "xdpvxlan"

#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/bpf.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <arpa/inet.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <stdbool.h>
#include "bpf_helpers.h"

struct vlan_hdr {
  __be16  h_vlan_TCI;
  __be16  h_vlan_encapsulated_proto;
};


#define u8 __u8
#define u16 __u16
#define u32 __u32
#define u64 __u64

char _license[] SEC("license") = "GPL";

#define DEBUG 1
#ifdef  DEBUG
/* Only use this for debug output. Notice output from bpf_trace_printk()
 * end-up in /sys/kernel/debug/tracing/trace_pipe
 */
#define bpf_debug(fmt, ...)						\
		({							\
			char ____fmt[] = fmt;				\
			bpf_trace_printk(____fmt, sizeof(____fmt),	\
				     ##__VA_ARGS__);			\
		})
#else
#define bpf_debug(fmt, ...) { } while (0)
#endif

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

struct bpf_map_def SEC("maps") qidconf_map = {
  .type = BPF_MAP_TYPE_ARRAY,
  .key_size = sizeof(int),
  .value_size = sizeof(int),
  .max_entries = 1,
};

struct bpf_map_def SEC("maps") xsk_map = {
  .type = BPF_MAP_TYPE_XSKMAP,
  .key_size = sizeof(int),
  .value_size = sizeof(int),
  .max_entries = 1,
};

/* Parse Ethernet layer 2, extract network layer 3 offset and protocol
 *
 * Returns false on error and non-supported ether-type
 */
static __always_inline
bool parse_eth(struct ethhdr *eth, void *data_end,
	       u16 *eth_proto, u64 *l3_offset)
{
	u16 eth_type;
	u64 offset;

	offset = sizeof(*eth);
	if ((void *)eth + offset > data_end)
		return false;

	eth_type = eth->h_proto;
	bpf_debug("Debug: eth_type:0x%x\n", ntohs(eth_type));

	/* Skip non 802.3 Ethertypes */
	if (unlikely(ntohs(eth_type) < ETH_P_802_3_MIN))
		return false;

	/* Handle VLAN tagged packet */
	if (eth_type == htons(ETH_P_8021Q) || eth_type == htons(ETH_P_8021AD)) {
		struct vlan_hdr *vlan_hdr;

		vlan_hdr = (void *)eth + offset;
		offset += sizeof(*vlan_hdr);
		if ((void *)eth + offset > data_end)
			return false;
		eth_type = vlan_hdr->h_vlan_encapsulated_proto;
	}
	/* Handle double VLAN tagged packet */
	if (eth_type == htons(ETH_P_8021Q) || eth_type == htons(ETH_P_8021AD)) {
		struct vlan_hdr *vlan_hdr;

		vlan_hdr = (void *)eth + offset;
		offset += sizeof(*vlan_hdr);
		if ((void *)eth + offset > data_end)
			return false;
		eth_type = vlan_hdr->h_vlan_encapsulated_proto;
	}

  bpf_debug("offset: %u\n", offset);

	*eth_proto = ntohs(eth_type);
	*l3_offset = offset;
	return true;
}

static __always_inline
u32 parse_port(struct xdp_md *ctx, u8 proto, void *hdr)
{
  void *data_end = (void *)(long)ctx->data_end;
  struct udphdr *udph;
  struct tcphdr *tcph;

  bpf_debug("ipv4 proto %u\n", proto);

  switch (proto) {
    case IPPROTO_UDP:
      udph = hdr;
      if (udph + 1 > (struct udphdr*)data_end) {
        bpf_debug("Invalid UDPv4 packet: L4off:%llu\n",
            sizeof(struct iphdr) + sizeof(struct udphdr));
        return XDP_ABORTED;
      }
      return ntohs(udph->dest);

    case IPPROTO_TCP:
      tcph = hdr;
      if (tcph + 1 > (struct tcphdr*)data_end) {
        bpf_debug("Invalid TCPv4 packet: L4off:%llu\n",
            sizeof(struct iphdr) + sizeof(struct tcphdr));
        return XDP_ABORTED;
      }
      return ntohs(tcph->dest);

    default:
      return 0;
  }

  return 0;
}

static __always_inline
u32 parse_ipv4(struct xdp_md *ctx, u64 l3_offset)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data     = (void *)(long)ctx->data;
	struct iphdr *iph = data + l3_offset;
	u32 ip_src; /* type need to match map */

	/* Hint: +1 is sizeof(struct iphdr) */
	if (iph + 1 > (struct iphdr*)data_end) {
		bpf_debug("Invalid IPv4 packet: L3off:%llu\n", l3_offset);
		return XDP_ABORTED;
	}
	/* Extract key */
	ip_src = iph->saddr;
	//ip_src = ntohl(ip_src); // ntohl does not work for some reason!?!

	bpf_debug("Valid IPv4 packet: raw saddr:0x%x\n", ip_src);

	return parse_port(ctx, iph->protocol, iph + 1);
}

static __always_inline
int handle_packet(struct xdp_md *ctx, u16 eth_proto, u64 l3_offset)
{
  u32 port;
	switch (eth_proto) {
	case ETH_P_ARP:  
    /* arps should go to ther kenel */
    bpf_debug("arp\n");
    return XDP_PASS;
	case ETH_P_IP:
    bpf_debug("ipv4\n");
		port = parse_ipv4(ctx, l3_offset);
    bpf_debug("ipv4 port %u\n", port);
    /* vxlan packets go to userspace */
    if(port == 4789) {
      return bpf_redirect_map(&xsk_map, 0, 0);
    }
    /* everybody else goes to the kernel */
    return XDP_PASS;
	case ETH_P_IPV6: /* No handler for IPv6 yet*/
    bpf_debug("ipv6\n");
	default:
		bpf_debug("Not handling eth_proto:0x%x\n", eth_proto);
		return XDP_PASS;
	}
	return XDP_PASS;
}

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

	return handle_packet(ctx, eth_proto, l3_offset);
}
