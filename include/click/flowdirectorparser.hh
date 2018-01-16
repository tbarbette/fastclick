/*-
 *   BSD LICENSE
 *
 *   Copyright 2016 6WIND S.A.
 *   Copyright 2016 Mellanox.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of 6WIND S.A. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef CLICK_FLOWDIRECTORPARSER_HH
#define CLICK_FLOWDIRECTORPARSER_HH

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <rte_common.h>
#include <rte_ethdev.h>
#include <rte_byteorder.h>
#include <rte_string_fns.h>
extern "C" {
    #include <cmdline_parse.h>
    #include <cmdline_parse_num.h>
    #include <cmdline_parse_string.h>
    #include <cmdline_parse_ipaddr.h>
    #include <cmdline_parse_etheraddr.h>
    #include <cmdline_flow.h>
}
#include <rte_flow.h>

CLICK_DECLS

#define RTE_PORT_STOPPED        (uint16_t)0
#define RTE_PORT_STARTED        (uint16_t)1
#define RTE_PORT_CLOSED         (uint16_t)2
#define RTE_PORT_HANDLING       (uint16_t)3

portid_t nb_ports;             /**< Number of probed ethernet ports. */

void fdir_set_flex_mask(portid_t port_id,
			struct rte_eth_fdir_flex_mask *cfg);
void fdir_set_flex_payload(portid_t port_id,
			struct rte_eth_flex_payload_cfg *cfg);
static void cmd_reconfig_device_queue(portid_t id,
			uint8_t dev, uint8_t queue);

struct rte_fdir_conf fdir_conf = {
	.mode = RTE_FDIR_MODE_NONE,
	.pballoc = RTE_FDIR_PBALLOC_64K,
	.status = RTE_FDIR_REPORT_STATUS,
	.drop_queue = 127,
	.mask = {
		.vlan_tci_mask = 0x0,
		.ipv4_mask     = {
			.src_ip = 0xFFFFFFFF,
			.dst_ip = 0xFFFFFFFF,
		},
		.ipv6_mask     = {
			.src_ip = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
			.dst_ip = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
		},
		.src_port_mask = 0xFFFF,
		.dst_port_mask = 0xFFFF,
		.mac_addr_byte_mask = 0xFF,
		.tunnel_id_mask = 0xFFFFFFFF,
		.tunnel_type_mask = 1,
	},
};

/* *** A 2tuple FILTER *** */
struct cmd_2tuple_filter_result {
	cmdline_fixed_string_t filter;
	portid_t port_id;
	cmdline_fixed_string_t ops;
	cmdline_fixed_string_t dst_port;
	uint16_t dst_port_value;
	cmdline_fixed_string_t protocol;
	uint8_t protocol_value;
	cmdline_fixed_string_t mask;
	uint8_t  mask_value;
	cmdline_fixed_string_t tcp_flags;
	uint8_t tcp_flags_value;
	cmdline_fixed_string_t priority;
	uint8_t  priority_value;
	cmdline_fixed_string_t queue;
	uint16_t  queue_id;
};

/* *** A 5tuple FILTER *** */
struct cmd_5tuple_filter_result {
	cmdline_fixed_string_t filter;
	portid_t port_id;
	cmdline_fixed_string_t ops;
	cmdline_fixed_string_t dst_ip;
	cmdline_ipaddr_t dst_ip_value;
	cmdline_fixed_string_t src_ip;
	cmdline_ipaddr_t src_ip_value;
	cmdline_fixed_string_t dst_port;
	uint16_t dst_port_value;
	cmdline_fixed_string_t src_port;
	uint16_t src_port_value;
	cmdline_fixed_string_t protocol;
	uint8_t protocol_value;
	cmdline_fixed_string_t mask;
	uint8_t  mask_value;
	cmdline_fixed_string_t tcp_flags;
	uint8_t tcp_flags_value;
	cmdline_fixed_string_t priority;
	uint8_t  priority_value;
	cmdline_fixed_string_t queue;
	uint16_t  queue_id;
};



class FlowDirectorParser {
	public:

};

CLICK_ENDDECLS
#endif
