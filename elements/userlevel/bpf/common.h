// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2019 - The MergeTB Authors */

#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <arpa/inet.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <stdbool.h>

#define u8 __u8
#define u16 __u16
#define u32 __u32
#define u64 __u64

struct vlan_hdr {
  __be16  h_vlan_TCI;
  __be16  h_vlan_encapsulated_proto;
};

/* VXLAN protocol (RFC 7348) header:
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |R|R|R|R|I|R|R|R|               Reserved                        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                VXLAN Network Identifier (VNI) |   Reserved    |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * I = VXLAN Network Identifier (VNI) present.
 */
struct vxlanhdr {
	__be32 vx_flags;
	__be32 vx_vni;
};


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
int handle_packet(struct bpf_map_def *xsk_map, struct xdp_md *ctx,
    u16 eth_proto, u64 l3_offset)
{
  u32 port;
	switch (eth_proto) {

  /* arp and ipv6 go to kernel */
	case ETH_P_ARP:  
    bpf_debug("arp\n");
	case ETH_P_IPV6: 
    bpf_debug("ipv6\n");
    return XDP_PASS;

	case ETH_P_IP:
    bpf_debug("ipv4\n");
		port = parse_ipv4(ctx, l3_offset);
    bpf_debug("ipv4 port %u\n", port);

    /* vxlan packets go to userspace */
    if(port == 4789) {
      return bpf_redirect_map(xsk_map, 0, 0);
    }
    /* everybody else goes to the kernel */
    return XDP_PASS;

	default:
		bpf_debug("Not handling eth_proto:0x%x\n", eth_proto);
		return XDP_PASS;

	}

}

static __always_inline
int handle_packet_vni(
  struct bpf_map_def *xsk_map, 
  struct bpf_map_def *vni_map, 
  struct xdp_md *ctx,
  u16 eth_proto, 
  u64 l3_offset
)
{
  u32 port;
  switch (eth_proto) {

    /* arp and ipv6 go to kernel */
    case ETH_P_ARP:  
      bpf_debug("arp\n");
    case ETH_P_IPV6: 
      bpf_debug("ipv6\n");
      return XDP_PASS;

    case ETH_P_IP:
      bpf_debug("ipv4\n");
      port = parse_ipv4(ctx, l3_offset);
      bpf_debug("ipv4 port %u\n", port);

      /* vxlan packets on mapped vnis go to userspace*/
      if(port == 4789) {

        /* extract the vni from the packet */
        void *data_end = (void*)(long)ctx->data_end;
        void *data     = (void*)(long)ctx->data;
        struct vxlanhdr *vxh = data + l3_offset + 28;

        if (vxh + 4 > (struct vxlanhdr*)data_end) {
          bpf_debug("Invalid VXLAN header: off:%llu\n", l3_offset + 28);
          return XDP_ABORTED;
        }

        u32 vni = (vxh->vx_vni & 0xFFFFFF00) >> 16;
        bpf_debug("vni: %lu\n", vni);

        /* try to find the associated xsk_map, if it's a thing send the packet
         * there, if not fall through to kernel network stack */
        int *q = bpf_map_lookup_elem(vni_map, &vni);
        if(q) {
          bpf_debug("vni %lu is in vni map, @sfd %d\n", vni, *q);
          return bpf_redirect_map(xsk_map, 47, 0);
        }
        else {
          bpf_debug("vni %lu is NOT in vni map\n", vni);
        }
      }
      
      /* everybody else goes to the kernel */
      return XDP_PASS;

    default:
      bpf_debug("Not handling eth_proto:0x%x\n", eth_proto);
      return XDP_PASS;

  }

}
