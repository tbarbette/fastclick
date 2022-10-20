/* -*- mode: c; c-basic-offset: 4 -*- */
#ifndef CLICKNET_IP6_H
#define CLICKNET_IP6_H
//#include <clicknet/ip.h>
/* get struct in6_addr */
#include <click/cxxprotect.h>
CLICK_CXX_PROTECT

#if CLICK_LINUXMODULE
# include <net/checksum.h>
# include <linux/in6.h>
#else
# include <sys/types.h>
# undef true
# include <netinet/in.h>
# define true linux_true
#endif

struct click_ip6 {
    union {
	struct {
	    uint32_t ip6_un1_flow;	/* 0-3	 bits 0-3: version == 6	     */
					/*	 bits 4-11: traffic class    */
					/*	   bits 4-9: DSCP	     */
					/*	   bits 10-11: ECN	     */
					/*	 bits 12-31: flow label	     */
	    uint16_t ip6_un1_plen;	/* 4-5	 payload length		     */
	    uint8_t ip6_un1_nxt;	/* 6	 next header		     */
	    uint8_t ip6_un1_hlim;	/* 7	 hop limit		     */
	} ip6_un1;
	uint8_t ip6_un2_vfc;		/* 0	 bits 0-3: version == 6	     */
					/*	 bits 4-7: top 4 class bits  */
	struct {
#if CLICK_BYTE_ORDER == CLICK_BIG_ENDIAN
	    unsigned ip6_un3_v : 4;	/* 0	 version == 6		     */
	    unsigned ip6_un3_fc : 4;	/*	 header length		     */
#elif CLICK_BYTE_ORDER == CLICK_LITTLE_ENDIAN
	    unsigned ip6_un3_fc : 4;	/* 0	 header length		     */
	    unsigned ip6_un3_v : 4;	/*	 version == 6		     */
#endif
	} ip6_un3;
    } ip6_ctlun;
    struct in6_addr ip6_src;	/* 8-23	 source address */
    struct in6_addr ip6_dst;	/* 24-39 dest address */
} CLICK_SIZE_PACKED_ATTRIBUTE;

struct click_ip6_eh {
	uint8_t nxt;
	uint8_t len;
} CLICK_SIZE_PACKED_ATTRIBUTE;

#define ip6_v			ip6_ctlun.ip6_un3.ip6_un3_v
#define ip6_vfc			ip6_ctlun.ip6_un2_vfc
#define ip6_flow		ip6_ctlun.ip6_un1.ip6_un1_flow
#define ip6_plen		ip6_ctlun.ip6_un1.ip6_un1_plen
#define ip6_nxt			ip6_ctlun.ip6_un1.ip6_un1_nxt
#define ip6_hlim		ip6_ctlun.ip6_un1.ip6_un1_hlim

#define IP6_EH_HOPBYHOP		 0
#define IP6_EH_ROUTING       43
#define IP6_EH_FRAGMENT		 44
#define IP6_EH_AH            51
#define IP6_EH_ESP           50
#define IP6_EH_NONXT         59
#define IP6_EH_DST           60
#define IP6_EH_MOBILITY      135

#define IP6_FLOW_MASK		0x000FFFFFU
#define IP6_FLOW_SHIFT		0
#define IP6_CLASS_MASK		0x0FF00000U
#define IP6_CLASS_SHIFT		20
#define IP6_DSCP_MASK		0x0FC00000U
#define IP6_DSCP_SHIFT		22
#define IP6_V_MASK		0xF0000000U
#define IP6_V_SHIFT		28

#define IP6_CHECK_V(hdr)	(((hdr).ip6_vfc & htonl(IP6_V_MASK)) == htonl(6 << IP6_V_SHIFT))

#ifndef IP6PROTO_FRAGMENT
#define IP6PROTO_FRAGMENT 0x2c
#endif
struct click_ip6_fragment {
    uint8_t ip6_frag_nxt;
    uint8_t ip6_frag_reserved;
    uint16_t ip6_frag_offset;
#define IP6_MF		0x0001
#define IP6_OFFMASK	0xFFF8
					/*	 bits 0-12: Fragment offset  */
					/*	 bit 13-14: reserved	     */
					/*	 bit 15: More Fragment	     */
    uint32_t ip6_frag_id;
} CLICK_SIZE_PACKED_ATTRIBUTE;

#ifndef IP6PROTO_SEGMENT_ROUTING
#define IP6PROTO_SEGMENT_ROUTING 4
#endif
struct click_ip6_sr {
	uint8_t ip6_sr_next;
	uint8_t ip6_hdrlen;
	uint8_t type; // Always 4 for Segment Routing
	uint8_t segment_left;
	uint8_t last_entry; // Number of segments in the Segment List
	uint8_t flags;
	uint16_t tag;
	struct in6_addr segments[0]; // Segment List (variable length)
} CLICK_SIZE_PACKED_ATTRIBUTE;

struct click_sr6_tlv {
	uint8_t type;
	uint8_t len;
	uint8_t value[0];
} CLICK_SIZE_PACKED_ATTRIBUTE;

uint16_t in6_fast_cksum(const struct in6_addr *saddr,
			const struct in6_addr *daddr,
			uint16_t len,
			uint8_t proto,
			uint16_t ori_csum,
			const unsigned char *addr,
			uint16_t len2);

uint16_t in6_cksum(const struct in6_addr *saddr,
		   const struct in6_addr *daddr,
		   uint16_t len,
		   uint8_t proto,
		   uint16_t ori_csum,
		   unsigned char *addr,
		   uint16_t len2);

CLICK_CXX_UNPROTECT
#include <click/cxxunprotect.h>
#endif
