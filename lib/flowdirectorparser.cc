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

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <click/config.h>
#include <click/dpdkdevice.hh>
#include <click/flowdirectorparser.hh>

CLICK_DECLS

/* *** ADD/REMOVE A 2tuple FILTER *** */
static void
cmd_2tuple_filter_parsed(void *parsed_result,
			__attribute__((unused)) struct cmdline *cl,
			__attribute__((unused)) void *data)
{
	struct rte_eth_ntuple_filter filter;
	struct cmd_2tuple_filter_result *res =
		(struct cmd_2tuple_filter_result *) parsed_result;
	int ret = 0;

	ret = rte_eth_dev_filter_supported(res->port_id, RTE_ETH_FILTER_NTUPLE);
	if (ret < 0) {
		printf("ntuple filter is not supported on port %u.\n",
			res->port_id);
		return;
	}

	memset(&filter, 0, sizeof(struct rte_eth_ntuple_filter));

	filter.flags = RTE_2TUPLE_FLAGS;
	filter.dst_port_mask = (res->mask_value & 0x02) ? UINT16_MAX : 0;
	filter.proto_mask = (res->mask_value & 0x01) ? UINT8_MAX : 0;
	filter.proto = res->protocol_value;
	filter.priority = res->priority_value;
	if (res->tcp_flags_value != 0 && filter.proto != IPPROTO_TCP) {
		printf("nonzero tcp_flags is only meaningful"
			" when protocol is TCP.\n");
		return;
	}
	if (res->tcp_flags_value > TCP_FLAG_ALL) {
		printf("invalid TCP flags.\n");
		return;
	}

	if (res->tcp_flags_value != 0) {
		filter.flags |= RTE_NTUPLE_FLAGS_TCP_FLAG;
		filter.tcp_flags = res->tcp_flags_value;
	}

	/* need convert to big endian. */
	filter.dst_port = rte_cpu_to_be_16(res->dst_port_value);
	filter.queue = res->queue_id;

	if (!strcmp(res->ops, "add"))
		ret = rte_eth_dev_filter_ctrl(res->port_id,
				RTE_ETH_FILTER_NTUPLE,
				RTE_ETH_FILTER_ADD,
				&filter);
	else
		ret = rte_eth_dev_filter_ctrl(res->port_id,
				RTE_ETH_FILTER_NTUPLE,
				RTE_ETH_FILTER_DELETE,
				&filter);
	if (ret < 0)
		printf("2tuple filter programming error: (%s)\n",
			strerror(-ret));

}

cmdline_parse_token_string_t cmd_2tuple_filter_filter =
	TOKEN_STRING_INITIALIZER(struct cmd_2tuple_filter_result,
				 filter, "2tuple_filter");
cmdline_parse_token_num_t cmd_2tuple_filter_port_id =
	TOKEN_NUM_INITIALIZER(struct cmd_2tuple_filter_result,
				port_id, UINT16);
cmdline_parse_token_string_t cmd_2tuple_filter_ops =
	TOKEN_STRING_INITIALIZER(struct cmd_2tuple_filter_result,
				 ops, "add#del");
cmdline_parse_token_string_t cmd_2tuple_filter_dst_port =
	TOKEN_STRING_INITIALIZER(struct cmd_2tuple_filter_result,
				dst_port, "dst_port");
cmdline_parse_token_num_t cmd_2tuple_filter_dst_port_value =
	TOKEN_NUM_INITIALIZER(struct cmd_2tuple_filter_result,
				dst_port_value, UINT16);
cmdline_parse_token_string_t cmd_2tuple_filter_protocol =
	TOKEN_STRING_INITIALIZER(struct cmd_2tuple_filter_result,
				protocol, "protocol");
cmdline_parse_token_num_t cmd_2tuple_filter_protocol_value =
	TOKEN_NUM_INITIALIZER(struct cmd_2tuple_filter_result,
				protocol_value, UINT8);
cmdline_parse_token_string_t cmd_2tuple_filter_mask =
	TOKEN_STRING_INITIALIZER(struct cmd_2tuple_filter_result,
				mask, "mask");
cmdline_parse_token_num_t cmd_2tuple_filter_mask_value =
	TOKEN_NUM_INITIALIZER(struct cmd_2tuple_filter_result,
				mask_value, INT8);
cmdline_parse_token_string_t cmd_2tuple_filter_tcp_flags =
	TOKEN_STRING_INITIALIZER(struct cmd_2tuple_filter_result,
				tcp_flags, "tcp_flags");
cmdline_parse_token_num_t cmd_2tuple_filter_tcp_flags_value =
	TOKEN_NUM_INITIALIZER(struct cmd_2tuple_filter_result,
				tcp_flags_value, UINT8);
cmdline_parse_token_string_t cmd_2tuple_filter_priority =
	TOKEN_STRING_INITIALIZER(struct cmd_2tuple_filter_result,
				priority, "priority");
cmdline_parse_token_num_t cmd_2tuple_filter_priority_value =
	TOKEN_NUM_INITIALIZER(struct cmd_2tuple_filter_result,
				priority_value, UINT8);
cmdline_parse_token_string_t cmd_2tuple_filter_queue =
	TOKEN_STRING_INITIALIZER(struct cmd_2tuple_filter_result,
				queue, "queue");
cmdline_parse_token_num_t cmd_2tuple_filter_queue_id =
	TOKEN_NUM_INITIALIZER(struct cmd_2tuple_filter_result,
				queue_id, UINT16);

cmdline_parse_inst_t cmd_2tuple_filter = {
	.f = cmd_2tuple_filter_parsed,
	.data = NULL,
	.help_str = "2tuple_filter <port_id> add|del dst_port <value> protocol "
		"<value> mask <value> tcp_flags <value> priority <value> queue "
		"<queue_id>: Add a 2tuple filter",
	.tokens = {
		(cmdline_parse_token_hdr_t *)&cmd_2tuple_filter_filter,
		(cmdline_parse_token_hdr_t *)&cmd_2tuple_filter_port_id,
		(cmdline_parse_token_hdr_t *)&cmd_2tuple_filter_ops,
		(cmdline_parse_token_hdr_t *)&cmd_2tuple_filter_dst_port,
		(cmdline_parse_token_hdr_t *)&cmd_2tuple_filter_dst_port_value,
		(cmdline_parse_token_hdr_t *)&cmd_2tuple_filter_protocol,
		(cmdline_parse_token_hdr_t *)&cmd_2tuple_filter_protocol_value,
		(cmdline_parse_token_hdr_t *)&cmd_2tuple_filter_mask,
		(cmdline_parse_token_hdr_t *)&cmd_2tuple_filter_mask_value,
		(cmdline_parse_token_hdr_t *)&cmd_2tuple_filter_tcp_flags,
		(cmdline_parse_token_hdr_t *)&cmd_2tuple_filter_tcp_flags_value,
		(cmdline_parse_token_hdr_t *)&cmd_2tuple_filter_priority,
		(cmdline_parse_token_hdr_t *)&cmd_2tuple_filter_priority_value,
		(cmdline_parse_token_hdr_t *)&cmd_2tuple_filter_queue,
		(cmdline_parse_token_hdr_t *)&cmd_2tuple_filter_queue_id,
		NULL,
	},
};

/* *** ADD/REMOVE A 5tuple FILTER *** */
static void
cmd_5tuple_filter_parsed(void *parsed_result,
			__attribute__((unused)) struct cmdline *cl,
			__attribute__((unused)) void *data)
{
	struct rte_eth_ntuple_filter filter;
	struct cmd_5tuple_filter_result *res =
		(struct cmd_5tuple_filter_result *) parsed_result;
	int ret = 0;

	ret = rte_eth_dev_filter_supported(res->port_id, RTE_ETH_FILTER_NTUPLE);
	if (ret < 0) {
		printf("ntuple filter is not supported on port %u.\n",
			res->port_id);
		return;
	}

	memset(&filter, 0, sizeof(struct rte_eth_ntuple_filter));

	filter.flags = RTE_5TUPLE_FLAGS;
	filter.dst_ip_mask = (res->mask_value & 0x10) ? UINT32_MAX : 0;
	filter.src_ip_mask = (res->mask_value & 0x08) ? UINT32_MAX : 0;
	filter.dst_port_mask = (res->mask_value & 0x04) ? UINT16_MAX : 0;
	filter.src_port_mask = (res->mask_value & 0x02) ? UINT16_MAX : 0;
	filter.proto_mask = (res->mask_value & 0x01) ? UINT8_MAX : 0;
	filter.proto = res->protocol_value;
	filter.priority = res->priority_value;
	if (res->tcp_flags_value != 0 && filter.proto != IPPROTO_TCP) {
		printf("nonzero tcp_flags is only meaningful"
			" when protocol is TCP.\n");
		return;
	}
	if (res->tcp_flags_value > TCP_FLAG_ALL) {
		printf("invalid TCP flags.\n");
		return;
	}

	if (res->tcp_flags_value != 0) {
		filter.flags |= RTE_NTUPLE_FLAGS_TCP_FLAG;
		filter.tcp_flags = res->tcp_flags_value;
	}

	if (res->dst_ip_value.family == AF_INET)
		/* no need to convert, already big endian. */
		filter.dst_ip = res->dst_ip_value.addr.ipv4.s_addr;
	else {
		if (filter.dst_ip_mask == 0) {
			printf("can not support ipv6 involved compare.\n");
			return;
		}
		filter.dst_ip = 0;
	}

	if (res->src_ip_value.family == AF_INET)
		/* no need to convert, already big endian. */
		filter.src_ip = res->src_ip_value.addr.ipv4.s_addr;
	else {
		if (filter.src_ip_mask == 0) {
			printf("can not support ipv6 involved compare.\n");
			return;
		}
		filter.src_ip = 0;
	}
	/* need convert to big endian. */
	filter.dst_port = rte_cpu_to_be_16(res->dst_port_value);
	filter.src_port = rte_cpu_to_be_16(res->src_port_value);
	filter.queue = res->queue_id;

	if (!strcmp(res->ops, "add"))
		ret = rte_eth_dev_filter_ctrl(res->port_id,
				RTE_ETH_FILTER_NTUPLE,
				RTE_ETH_FILTER_ADD,
				&filter);
	else
		ret = rte_eth_dev_filter_ctrl(res->port_id,
				RTE_ETH_FILTER_NTUPLE,
				RTE_ETH_FILTER_DELETE,
				&filter);
	if (ret < 0)
		printf("5tuple filter programming error: (%s)\n",
			strerror(-ret));
}

cmdline_parse_token_string_t cmd_5tuple_filter_filter =
	TOKEN_STRING_INITIALIZER(struct cmd_5tuple_filter_result,
				 filter, "5tuple_filter");
cmdline_parse_token_num_t cmd_5tuple_filter_port_id =
	TOKEN_NUM_INITIALIZER(struct cmd_5tuple_filter_result,
				port_id, UINT16);
cmdline_parse_token_string_t cmd_5tuple_filter_ops =
	TOKEN_STRING_INITIALIZER(struct cmd_5tuple_filter_result,
				 ops, "add#del");
cmdline_parse_token_string_t cmd_5tuple_filter_dst_ip =
	TOKEN_STRING_INITIALIZER(struct cmd_5tuple_filter_result,
				dst_ip, "dst_ip");
cmdline_parse_token_ipaddr_t cmd_5tuple_filter_dst_ip_value =
	TOKEN_IPADDR_INITIALIZER(struct cmd_5tuple_filter_result,
				dst_ip_value);
cmdline_parse_token_string_t cmd_5tuple_filter_src_ip =
	TOKEN_STRING_INITIALIZER(struct cmd_5tuple_filter_result,
				src_ip, "src_ip");
cmdline_parse_token_ipaddr_t cmd_5tuple_filter_src_ip_value =
	TOKEN_IPADDR_INITIALIZER(struct cmd_5tuple_filter_result,
				src_ip_value);
cmdline_parse_token_string_t cmd_5tuple_filter_dst_port =
	TOKEN_STRING_INITIALIZER(struct cmd_5tuple_filter_result,
				dst_port, "dst_port");
cmdline_parse_token_num_t cmd_5tuple_filter_dst_port_value =
	TOKEN_NUM_INITIALIZER(struct cmd_5tuple_filter_result,
				dst_port_value, UINT16);
cmdline_parse_token_string_t cmd_5tuple_filter_src_port =
	TOKEN_STRING_INITIALIZER(struct cmd_5tuple_filter_result,
				src_port, "src_port");
cmdline_parse_token_num_t cmd_5tuple_filter_src_port_value =
	TOKEN_NUM_INITIALIZER(struct cmd_5tuple_filter_result,
				src_port_value, UINT16);
cmdline_parse_token_string_t cmd_5tuple_filter_protocol =
	TOKEN_STRING_INITIALIZER(struct cmd_5tuple_filter_result,
				protocol, "protocol");
cmdline_parse_token_num_t cmd_5tuple_filter_protocol_value =
	TOKEN_NUM_INITIALIZER(struct cmd_5tuple_filter_result,
				protocol_value, UINT8);
cmdline_parse_token_string_t cmd_5tuple_filter_mask =
	TOKEN_STRING_INITIALIZER(struct cmd_5tuple_filter_result,
				mask, "mask");
cmdline_parse_token_num_t cmd_5tuple_filter_mask_value =
	TOKEN_NUM_INITIALIZER(struct cmd_5tuple_filter_result,
				mask_value, INT8);
cmdline_parse_token_string_t cmd_5tuple_filter_tcp_flags =
	TOKEN_STRING_INITIALIZER(struct cmd_5tuple_filter_result,
				tcp_flags, "tcp_flags");
cmdline_parse_token_num_t cmd_5tuple_filter_tcp_flags_value =
	TOKEN_NUM_INITIALIZER(struct cmd_5tuple_filter_result,
				tcp_flags_value, UINT8);
cmdline_parse_token_string_t cmd_5tuple_filter_priority =
	TOKEN_STRING_INITIALIZER(struct cmd_5tuple_filter_result,
				priority, "priority");
cmdline_parse_token_num_t cmd_5tuple_filter_priority_value =
	TOKEN_NUM_INITIALIZER(struct cmd_5tuple_filter_result,
				priority_value, UINT8);
cmdline_parse_token_string_t cmd_5tuple_filter_queue =
	TOKEN_STRING_INITIALIZER(struct cmd_5tuple_filter_result,
				queue, "queue");
cmdline_parse_token_num_t cmd_5tuple_filter_queue_id =
	TOKEN_NUM_INITIALIZER(struct cmd_5tuple_filter_result,
				queue_id, UINT16);

cmdline_parse_inst_t cmd_5tuple_filter = {
	.f = cmd_5tuple_filter_parsed,
	.data = NULL,
	.help_str = "5tuple_filter <port_id> add|del dst_ip <value> "
		"src_ip <value> dst_port <value> src_port <value> "
		"protocol <value>  mask <value> tcp_flags <value> "
		"priority <value> queue <queue_id>: Add/Del a 5tuple filter",
	.tokens = {
		(cmdline_parse_token_hdr_t *)&cmd_5tuple_filter_filter,
		(cmdline_parse_token_hdr_t *)&cmd_5tuple_filter_port_id,
		(cmdline_parse_token_hdr_t *)&cmd_5tuple_filter_ops,
		(cmdline_parse_token_hdr_t *)&cmd_5tuple_filter_dst_ip,
		(cmdline_parse_token_hdr_t *)&cmd_5tuple_filter_dst_ip_value,
		(cmdline_parse_token_hdr_t *)&cmd_5tuple_filter_src_ip,
		(cmdline_parse_token_hdr_t *)&cmd_5tuple_filter_src_ip_value,
		(cmdline_parse_token_hdr_t *)&cmd_5tuple_filter_dst_port,
		(cmdline_parse_token_hdr_t *)&cmd_5tuple_filter_dst_port_value,
		(cmdline_parse_token_hdr_t *)&cmd_5tuple_filter_src_port,
		(cmdline_parse_token_hdr_t *)&cmd_5tuple_filter_src_port_value,
		(cmdline_parse_token_hdr_t *)&cmd_5tuple_filter_protocol,
		(cmdline_parse_token_hdr_t *)&cmd_5tuple_filter_protocol_value,
		(cmdline_parse_token_hdr_t *)&cmd_5tuple_filter_mask,
		(cmdline_parse_token_hdr_t *)&cmd_5tuple_filter_mask_value,
		(cmdline_parse_token_hdr_t *)&cmd_5tuple_filter_tcp_flags,
		(cmdline_parse_token_hdr_t *)&cmd_5tuple_filter_tcp_flags_value,
		(cmdline_parse_token_hdr_t *)&cmd_5tuple_filter_priority,
		(cmdline_parse_token_hdr_t *)&cmd_5tuple_filter_priority_value,
		(cmdline_parse_token_hdr_t *)&cmd_5tuple_filter_queue,
		(cmdline_parse_token_hdr_t *)&cmd_5tuple_filter_queue_id,
		NULL,
	},
};

/* *** ADD/REMOVE A flex FILTER *** */
struct cmd_flex_filter_result {
	cmdline_fixed_string_t filter;
	cmdline_fixed_string_t ops;
	portid_t port_id;
	cmdline_fixed_string_t len;
	uint8_t len_value;
	cmdline_fixed_string_t bytes;
	cmdline_fixed_string_t bytes_value;
	cmdline_fixed_string_t mask;
	cmdline_fixed_string_t mask_value;
	cmdline_fixed_string_t priority;
	uint8_t priority_value;
	cmdline_fixed_string_t queue;
	uint16_t queue_id;
};

static int xdigit2val(unsigned char c)
{
	int val;
	if (isdigit(c))
		val = c - '0';
	else if (isupper(c))
		val = c - 'A' + 10;
	else
		val = c - 'a' + 10;
	return val;
}

static void
cmd_flex_filter_parsed(void *parsed_result,
			  __attribute__((unused)) struct cmdline *cl,
			  __attribute__((unused)) void *data)
{
	int ret = 0;
	struct rte_eth_flex_filter filter;
	struct cmd_flex_filter_result *res =
		(struct cmd_flex_filter_result *) parsed_result;
	char *bytes_ptr, *mask_ptr;
	uint16_t len, i, j = 0;
	char c;
	int val;
	uint8_t byte = 0;

	if (res->len_value > RTE_FLEX_FILTER_MAXLEN) {
		printf("the len exceed the max length 128\n");
		return;
	}
	memset(&filter, 0, sizeof(struct rte_eth_flex_filter));
	filter.len = res->len_value;
	filter.priority = res->priority_value;
	filter.queue = res->queue_id;
	bytes_ptr = res->bytes_value;
	mask_ptr = res->mask_value;

	 /* translate bytes string to array. */
	if (bytes_ptr[0] == '0' && ((bytes_ptr[1] == 'x') ||
		(bytes_ptr[1] == 'X')))
		bytes_ptr += 2;
	len = strnlen(bytes_ptr, res->len_value * 2);
	if (len == 0 || (len % 8 != 0)) {
		printf("please check len and bytes input\n");
		return;
	}
	for (i = 0; i < len; i++) {
		c = bytes_ptr[i];
		if (isxdigit(c) == 0) {
			/* invalid characters. */
			printf("invalid input\n");
			return;
		}
		val = xdigit2val(c);
		if (i % 2) {
			byte |= val;
			filter.bytes[j] = byte;
			printf("bytes[%d]:%02x ", j, filter.bytes[j]);
			j++;
			byte = 0;
		} else
			byte |= val << 4;
	}
	printf("\n");
	 /* translate mask string to uint8_t array. */
	if (mask_ptr[0] == '0' && ((mask_ptr[1] == 'x') ||
		(mask_ptr[1] == 'X')))
		mask_ptr += 2;
	len = strnlen(mask_ptr, (res->len_value + 3) / 4);
	if (len == 0) {
		printf("invalid input\n");
		return;
	}
	j = 0;
	byte = 0;
	for (i = 0; i < len; i++) {
		c = mask_ptr[i];
		if (isxdigit(c) == 0) {
			/* invalid characters. */
			printf("invalid input\n");
			return;
		}
		val = xdigit2val(c);
		if (i % 2) {
			byte |= val;
			filter.mask[j] = byte;
			printf("mask[%d]:%02x ", j, filter.mask[j]);
			j++;
			byte = 0;
		} else
			byte |= val << 4;
	}
	printf("\n");

	if (!strcmp(res->ops, "add"))
		ret = rte_eth_dev_filter_ctrl(res->port_id,
				RTE_ETH_FILTER_FLEXIBLE,
				RTE_ETH_FILTER_ADD,
				&filter);
	else
		ret = rte_eth_dev_filter_ctrl(res->port_id,
				RTE_ETH_FILTER_FLEXIBLE,
				RTE_ETH_FILTER_DELETE,
				&filter);

	if (ret < 0)
		printf("flex filter setting error: (%s)\n", strerror(-ret));
}

cmdline_parse_token_string_t cmd_flex_filter_filter =
	TOKEN_STRING_INITIALIZER(struct cmd_flex_filter_result,
				filter, "flex_filter");
cmdline_parse_token_num_t cmd_flex_filter_port_id =
	TOKEN_NUM_INITIALIZER(struct cmd_flex_filter_result,
				port_id, UINT16);
cmdline_parse_token_string_t cmd_flex_filter_ops =
	TOKEN_STRING_INITIALIZER(struct cmd_flex_filter_result,
				ops, "add#del");
cmdline_parse_token_string_t cmd_flex_filter_len =
	TOKEN_STRING_INITIALIZER(struct cmd_flex_filter_result,
				len, "len");
cmdline_parse_token_num_t cmd_flex_filter_len_value =
	TOKEN_NUM_INITIALIZER(struct cmd_flex_filter_result,
				len_value, UINT8);
cmdline_parse_token_string_t cmd_flex_filter_bytes =
	TOKEN_STRING_INITIALIZER(struct cmd_flex_filter_result,
				bytes, "bytes");
cmdline_parse_token_string_t cmd_flex_filter_bytes_value =
	TOKEN_STRING_INITIALIZER(struct cmd_flex_filter_result,
				bytes_value, NULL);
cmdline_parse_token_string_t cmd_flex_filter_mask =
	TOKEN_STRING_INITIALIZER(struct cmd_flex_filter_result,
				mask, "mask");
cmdline_parse_token_string_t cmd_flex_filter_mask_value =
	TOKEN_STRING_INITIALIZER(struct cmd_flex_filter_result,
				mask_value, NULL);
cmdline_parse_token_string_t cmd_flex_filter_priority =
	TOKEN_STRING_INITIALIZER(struct cmd_flex_filter_result,
				priority, "priority");
cmdline_parse_token_num_t cmd_flex_filter_priority_value =
	TOKEN_NUM_INITIALIZER(struct cmd_flex_filter_result,
				priority_value, UINT8);
cmdline_parse_token_string_t cmd_flex_filter_queue =
	TOKEN_STRING_INITIALIZER(struct cmd_flex_filter_result,
				queue, "queue");
cmdline_parse_token_num_t cmd_flex_filter_queue_id =
	TOKEN_NUM_INITIALIZER(struct cmd_flex_filter_result,
				queue_id, UINT16);
cmdline_parse_inst_t cmd_flex_filter = {
	.f = cmd_flex_filter_parsed,
	.data = NULL,
	.help_str = "flex_filter <port_id> add|del len <value> bytes "
		"<value> mask <value> priority <value> queue <queue_id>: "
		"Add/Del a flex filter",
	.tokens = {
		(cmdline_parse_token_hdr_t *)&cmd_flex_filter_filter,
		(cmdline_parse_token_hdr_t *)&cmd_flex_filter_port_id,
		(cmdline_parse_token_hdr_t *)&cmd_flex_filter_ops,
		(cmdline_parse_token_hdr_t *)&cmd_flex_filter_len,
		(cmdline_parse_token_hdr_t *)&cmd_flex_filter_len_value,
		(cmdline_parse_token_hdr_t *)&cmd_flex_filter_bytes,
		(cmdline_parse_token_hdr_t *)&cmd_flex_filter_bytes_value,
		(cmdline_parse_token_hdr_t *)&cmd_flex_filter_mask,
		(cmdline_parse_token_hdr_t *)&cmd_flex_filter_mask_value,
		(cmdline_parse_token_hdr_t *)&cmd_flex_filter_priority,
		(cmdline_parse_token_hdr_t *)&cmd_flex_filter_priority_value,
		(cmdline_parse_token_hdr_t *)&cmd_flex_filter_queue,
		(cmdline_parse_token_hdr_t *)&cmd_flex_filter_queue_id,
		NULL,
	},
};

/* *** Filters Control *** */

/* *** deal with ethertype filter *** */
struct cmd_ethertype_filter_result {
	cmdline_fixed_string_t filter;
	portid_t port_id;
	cmdline_fixed_string_t ops;
	cmdline_fixed_string_t mac;
	struct ether_addr mac_addr;
	cmdline_fixed_string_t ethertype;
	uint16_t ethertype_value;
	cmdline_fixed_string_t drop;
	cmdline_fixed_string_t queue;
	uint16_t  queue_id;
};

cmdline_parse_token_string_t cmd_ethertype_filter_filter =
	TOKEN_STRING_INITIALIZER(struct cmd_ethertype_filter_result,
				 filter, "ethertype_filter");
cmdline_parse_token_num_t cmd_ethertype_filter_port_id =
	TOKEN_NUM_INITIALIZER(struct cmd_ethertype_filter_result,
			      port_id, UINT16);
cmdline_parse_token_string_t cmd_ethertype_filter_ops =
	TOKEN_STRING_INITIALIZER(struct cmd_ethertype_filter_result,
				 ops, "add#del");
cmdline_parse_token_string_t cmd_ethertype_filter_mac =
	TOKEN_STRING_INITIALIZER(struct cmd_ethertype_filter_result,
				 mac, "mac_addr#mac_ignr");
cmdline_parse_token_etheraddr_t cmd_ethertype_filter_mac_addr =
	TOKEN_ETHERADDR_INITIALIZER(struct cmd_ethertype_filter_result,
				     mac_addr);
cmdline_parse_token_string_t cmd_ethertype_filter_ethertype =
	TOKEN_STRING_INITIALIZER(struct cmd_ethertype_filter_result,
				 ethertype, "ethertype");
cmdline_parse_token_num_t cmd_ethertype_filter_ethertype_value =
	TOKEN_NUM_INITIALIZER(struct cmd_ethertype_filter_result,
			      ethertype_value, UINT16);
cmdline_parse_token_string_t cmd_ethertype_filter_drop =
	TOKEN_STRING_INITIALIZER(struct cmd_ethertype_filter_result,
				 drop, "drop#fwd");
cmdline_parse_token_string_t cmd_ethertype_filter_queue =
	TOKEN_STRING_INITIALIZER(struct cmd_ethertype_filter_result,
				 queue, "queue");
cmdline_parse_token_num_t cmd_ethertype_filter_queue_id =
	TOKEN_NUM_INITIALIZER(struct cmd_ethertype_filter_result,
			      queue_id, UINT16);

static void
cmd_ethertype_filter_parsed(void *parsed_result,
			  __attribute__((unused)) struct cmdline *cl,
			  __attribute__((unused)) void *data)
{
	struct cmd_ethertype_filter_result *res =
		(struct cmd_ethertype_filter_result *) parsed_result;
	struct rte_eth_ethertype_filter filter;
	int ret = 0;

	ret = rte_eth_dev_filter_supported(res->port_id,
			RTE_ETH_FILTER_ETHERTYPE);
	if (ret < 0) {
		printf("ethertype filter is not supported on port %u.\n",
			res->port_id);
		return;
	}

	memset(&filter, 0, sizeof(filter));
	if (!strcmp(res->mac, "mac_addr")) {
		filter.flags |= RTE_ETHTYPE_FLAGS_MAC;
		rte_memcpy(&filter.mac_addr, &res->mac_addr,
			sizeof(struct ether_addr));
	}
	if (!strcmp(res->drop, "drop"))
		filter.flags |= RTE_ETHTYPE_FLAGS_DROP;
	filter.ether_type = res->ethertype_value;
	filter.queue = res->queue_id;

	if (!strcmp(res->ops, "add"))
		ret = rte_eth_dev_filter_ctrl(res->port_id,
				RTE_ETH_FILTER_ETHERTYPE,
				RTE_ETH_FILTER_ADD,
				&filter);
	else
		ret = rte_eth_dev_filter_ctrl(res->port_id,
				RTE_ETH_FILTER_ETHERTYPE,
				RTE_ETH_FILTER_DELETE,
				&filter);
	if (ret < 0)
		printf("ethertype filter programming error: (%s)\n",
			strerror(-ret));
}

cmdline_parse_inst_t cmd_ethertype_filter = {
	.f = cmd_ethertype_filter_parsed,
	.data = NULL,
	.help_str = "ethertype_filter <port_id> add|del mac_addr|mac_ignr "
		"<mac_addr> ethertype <value> drop|fw queue <queue_id>: "
		"Add or delete an ethertype filter entry",
	.tokens = {
		(cmdline_parse_token_hdr_t *)&cmd_ethertype_filter_filter,
		(cmdline_parse_token_hdr_t *)&cmd_ethertype_filter_port_id,
		(cmdline_parse_token_hdr_t *)&cmd_ethertype_filter_ops,
		(cmdline_parse_token_hdr_t *)&cmd_ethertype_filter_mac,
		(cmdline_parse_token_hdr_t *)&cmd_ethertype_filter_mac_addr,
		(cmdline_parse_token_hdr_t *)&cmd_ethertype_filter_ethertype,
		(cmdline_parse_token_hdr_t *)&cmd_ethertype_filter_ethertype_value,
		(cmdline_parse_token_hdr_t *)&cmd_ethertype_filter_drop,
		(cmdline_parse_token_hdr_t *)&cmd_ethertype_filter_queue,
		(cmdline_parse_token_hdr_t *)&cmd_ethertype_filter_queue_id,
		NULL,
	},
};

/* *** deal with flow director filter *** */
struct cmd_flow_director_result {
	cmdline_fixed_string_t flow_director_filter;
	portid_t port_id;
	cmdline_fixed_string_t mode;
	cmdline_fixed_string_t mode_value;
	cmdline_fixed_string_t ops;
	cmdline_fixed_string_t flow;
	cmdline_fixed_string_t flow_type;
	cmdline_fixed_string_t ether;
	uint16_t ether_type;
	cmdline_fixed_string_t src;
	cmdline_ipaddr_t ip_src;
	uint16_t port_src;
	cmdline_fixed_string_t dst;
	cmdline_ipaddr_t ip_dst;
	uint16_t port_dst;
	cmdline_fixed_string_t verify_tag;
	uint32_t verify_tag_value;
	cmdline_ipaddr_t tos;
	uint8_t tos_value;
	cmdline_ipaddr_t proto;
	uint8_t proto_value;
	cmdline_ipaddr_t ttl;
	uint8_t ttl_value;
	cmdline_fixed_string_t vlan;
	uint16_t vlan_value;
	cmdline_fixed_string_t flexbytes;
	cmdline_fixed_string_t flexbytes_value;
	cmdline_fixed_string_t pf_vf;
	cmdline_fixed_string_t drop;
	cmdline_fixed_string_t queue;
	uint16_t  queue_id;
	cmdline_fixed_string_t fd_id;
	uint32_t  fd_id_value;
	cmdline_fixed_string_t mac;
	struct ether_addr mac_addr;
	cmdline_fixed_string_t tunnel;
	cmdline_fixed_string_t tunnel_type;
	cmdline_fixed_string_t tunnel_id;
	uint32_t tunnel_id_value;
};

static inline int
parse_flexbytes(const char *q_arg, uint8_t *flexbytes, uint16_t max_num)
{
	char s[256];
	const char *p, *p0 = q_arg;
	char *end;
	unsigned long int_fld;
	char *str_fld[max_num];
	int i;
	unsigned size;
	int ret = -1;

	p = strchr(p0, '(');
	if (p == NULL)
		return -1;
	++p;
	p0 = strchr(p, ')');
	if (p0 == NULL)
		return -1;

	size = p0 - p;
	if (size >= sizeof(s))
		return -1;

	snprintf(s, sizeof(s), "%.*s", size, p);
	ret = rte_strsplit(s, sizeof(s), str_fld, max_num, ',');
	if (ret < 0 || ret > max_num)
		return -1;
	for (i = 0; i < ret; i++) {
		errno = 0;
		int_fld = strtoul(str_fld[i], &end, 0);
		if (errno != 0 || *end != '\0' || int_fld > UINT8_MAX)
			return -1;
		flexbytes[i] = (uint8_t)int_fld;
	}
	return ret;
}

static uint16_t
str2flowtype(char *string)
{
	uint8_t i = 0;
	static const struct {
		char str[32];
		uint16_t type;
	} flowtype_str[] = {
		{"raw", RTE_ETH_FLOW_RAW},
		{"ipv4", RTE_ETH_FLOW_IPV4},
		{"ipv4-frag", RTE_ETH_FLOW_FRAG_IPV4},
		{"ipv4-tcp", RTE_ETH_FLOW_NONFRAG_IPV4_TCP},
		{"ipv4-udp", RTE_ETH_FLOW_NONFRAG_IPV4_UDP},
		{"ipv4-sctp", RTE_ETH_FLOW_NONFRAG_IPV4_SCTP},
		{"ipv4-other", RTE_ETH_FLOW_NONFRAG_IPV4_OTHER},
		{"ipv6", RTE_ETH_FLOW_IPV6},
		{"ipv6-frag", RTE_ETH_FLOW_FRAG_IPV6},
		{"ipv6-tcp", RTE_ETH_FLOW_NONFRAG_IPV6_TCP},
		{"ipv6-udp", RTE_ETH_FLOW_NONFRAG_IPV6_UDP},
		{"ipv6-sctp", RTE_ETH_FLOW_NONFRAG_IPV6_SCTP},
		{"ipv6-other", RTE_ETH_FLOW_NONFRAG_IPV6_OTHER},
		{"l2_payload", RTE_ETH_FLOW_L2_PAYLOAD},
	};

	for (i = 0; i < RTE_DIM(flowtype_str); i++) {
		if (!strcmp(flowtype_str[i].str, string))
			return flowtype_str[i].type;
	}

	if (isdigit(string[0]) && atoi(string) > 0 && atoi(string) < 64)
		return (uint16_t)atoi(string);

	return RTE_ETH_FLOW_UNKNOWN;
}

static enum rte_eth_fdir_tunnel_type
str2fdir_tunneltype(char *string)
{
	uint8_t i = 0;

	static const struct {
		char str[32];
		enum rte_eth_fdir_tunnel_type type;
	} tunneltype_str[] = {
		{"NVGRE", RTE_FDIR_TUNNEL_TYPE_NVGRE},
		{"VxLAN", RTE_FDIR_TUNNEL_TYPE_VXLAN},
	};

	for (i = 0; i < RTE_DIM(tunneltype_str); i++) {
		if (!strcmp(tunneltype_str[i].str, string))
			return tunneltype_str[i].type;
	}
	return RTE_FDIR_TUNNEL_TYPE_UNKNOWN;
}

#define IPV4_ADDR_TO_UINT(ip_addr, ip) \
do { \
	if ((ip_addr).family == AF_INET) \
		(ip) = (ip_addr).addr.ipv4.s_addr; \
	else { \
		printf("invalid parameter.\n"); \
		return; \
	} \
} while (0)

#define IPV6_ADDR_TO_ARRAY(ip_addr, ip) \
do { \
	if ((ip_addr).family == AF_INET6) \
		rte_memcpy(&(ip), \
				 &((ip_addr).addr.ipv6), \
				 sizeof(struct in6_addr)); \
	else { \
		printf("invalid parameter.\n"); \
		return; \
	} \
} while (0)

static void
cmd_flow_director_filter_parsed(void *parsed_result,
			  __attribute__((unused)) struct cmdline *cl,
			  __attribute__((unused)) void *data)
{
	struct cmd_flow_director_result *res =
		(struct cmd_flow_director_result *) parsed_result;
	struct rte_eth_fdir_filter entry;
	uint8_t flexbytes[RTE_ETH_FDIR_MAX_FLEXLEN];
	char *end;
	unsigned long vf_id;
	int ret = 0;

	ret = rte_eth_dev_filter_supported(res->port_id, RTE_ETH_FILTER_FDIR);
	if (ret < 0) {
		printf("flow director is not supported on port %u.\n",
			res->port_id);
		return;
	}
	memset(flexbytes, 0, sizeof(flexbytes));
	memset(&entry, 0, sizeof(struct rte_eth_fdir_filter));

	if (fdir_conf.mode ==  RTE_FDIR_MODE_PERFECT_MAC_VLAN) {
		if (strcmp(res->mode_value, "MAC-VLAN")) {
			printf("Please set mode to MAC-VLAN.\n");
			return;
		}
	} else if (fdir_conf.mode ==  RTE_FDIR_MODE_PERFECT_TUNNEL) {
		if (strcmp(res->mode_value, "Tunnel")) {
			printf("Please set mode to Tunnel.\n");
			return;
		}
	} else {
		if (strcmp(res->mode_value, "IP")) {
			printf("Please set mode to IP.\n");
			return;
		}
		entry.input.flow_type = str2flowtype(res->flow_type);
	}

	ret = parse_flexbytes(res->flexbytes_value,
					flexbytes,
					RTE_ETH_FDIR_MAX_FLEXLEN);
	if (ret < 0) {
		printf("error: Cannot parse flexbytes input.\n");
		return;
	}

	switch (entry.input.flow_type) {
	case RTE_ETH_FLOW_FRAG_IPV4:
	case RTE_ETH_FLOW_NONFRAG_IPV4_OTHER:
		entry.input.flow.ip4_flow.proto = res->proto_value;
		/* fall-through */
	case RTE_ETH_FLOW_NONFRAG_IPV4_UDP:
	case RTE_ETH_FLOW_NONFRAG_IPV4_TCP:
		IPV4_ADDR_TO_UINT(res->ip_dst,
			entry.input.flow.ip4_flow.dst_ip);
		IPV4_ADDR_TO_UINT(res->ip_src,
			entry.input.flow.ip4_flow.src_ip);
		entry.input.flow.ip4_flow.tos = res->tos_value;
		entry.input.flow.ip4_flow.ttl = res->ttl_value;
		/* need convert to big endian. */
		entry.input.flow.udp4_flow.dst_port =
				rte_cpu_to_be_16(res->port_dst);
		entry.input.flow.udp4_flow.src_port =
				rte_cpu_to_be_16(res->port_src);
		break;
	case RTE_ETH_FLOW_NONFRAG_IPV4_SCTP:
		IPV4_ADDR_TO_UINT(res->ip_dst,
			entry.input.flow.sctp4_flow.ip.dst_ip);
		IPV4_ADDR_TO_UINT(res->ip_src,
			entry.input.flow.sctp4_flow.ip.src_ip);
		entry.input.flow.ip4_flow.tos = res->tos_value;
		entry.input.flow.ip4_flow.ttl = res->ttl_value;
		/* need convert to big endian. */
		entry.input.flow.sctp4_flow.dst_port =
				rte_cpu_to_be_16(res->port_dst);
		entry.input.flow.sctp4_flow.src_port =
				rte_cpu_to_be_16(res->port_src);
		entry.input.flow.sctp4_flow.verify_tag =
				rte_cpu_to_be_32(res->verify_tag_value);
		break;
	case RTE_ETH_FLOW_FRAG_IPV6:
	case RTE_ETH_FLOW_NONFRAG_IPV6_OTHER:
		entry.input.flow.ipv6_flow.proto = res->proto_value;
		/* fall-through */
	case RTE_ETH_FLOW_NONFRAG_IPV6_UDP:
	case RTE_ETH_FLOW_NONFRAG_IPV6_TCP:
		IPV6_ADDR_TO_ARRAY(res->ip_dst,
			entry.input.flow.ipv6_flow.dst_ip);
		IPV6_ADDR_TO_ARRAY(res->ip_src,
			entry.input.flow.ipv6_flow.src_ip);
		entry.input.flow.ipv6_flow.tc = res->tos_value;
		entry.input.flow.ipv6_flow.hop_limits = res->ttl_value;
		/* need convert to big endian. */
		entry.input.flow.udp6_flow.dst_port =
				rte_cpu_to_be_16(res->port_dst);
		entry.input.flow.udp6_flow.src_port =
				rte_cpu_to_be_16(res->port_src);
		break;
	case RTE_ETH_FLOW_NONFRAG_IPV6_SCTP:
		IPV6_ADDR_TO_ARRAY(res->ip_dst,
			entry.input.flow.sctp6_flow.ip.dst_ip);
		IPV6_ADDR_TO_ARRAY(res->ip_src,
			entry.input.flow.sctp6_flow.ip.src_ip);
		entry.input.flow.ipv6_flow.tc = res->tos_value;
		entry.input.flow.ipv6_flow.hop_limits = res->ttl_value;
		/* need convert to big endian. */
		entry.input.flow.sctp6_flow.dst_port =
				rte_cpu_to_be_16(res->port_dst);
		entry.input.flow.sctp6_flow.src_port =
				rte_cpu_to_be_16(res->port_src);
		entry.input.flow.sctp6_flow.verify_tag =
				rte_cpu_to_be_32(res->verify_tag_value);
		break;
	case RTE_ETH_FLOW_L2_PAYLOAD:
		entry.input.flow.l2_flow.ether_type =
			rte_cpu_to_be_16(res->ether_type);
		break;
	default:
		break;
	}

	if (fdir_conf.mode ==  RTE_FDIR_MODE_PERFECT_MAC_VLAN)
		rte_memcpy(&entry.input.flow.mac_vlan_flow.mac_addr,
				 &res->mac_addr,
				 sizeof(struct ether_addr));

	if (fdir_conf.mode ==  RTE_FDIR_MODE_PERFECT_TUNNEL) {
		rte_memcpy(&entry.input.flow.tunnel_flow.mac_addr,
				 &res->mac_addr,
				 sizeof(struct ether_addr));
		entry.input.flow.tunnel_flow.tunnel_type =
			str2fdir_tunneltype(res->tunnel_type);
		entry.input.flow.tunnel_flow.tunnel_id =
			rte_cpu_to_be_32(res->tunnel_id_value);
	}

	rte_memcpy(entry.input.flow_ext.flexbytes,
		   flexbytes,
		   RTE_ETH_FDIR_MAX_FLEXLEN);

	entry.input.flow_ext.vlan_tci = rte_cpu_to_be_16(res->vlan_value);

	entry.action.flex_off = 0;  /*use 0 by default */
	if (!strcmp(res->drop, "drop"))
		entry.action.behavior = RTE_ETH_FDIR_REJECT;
	else
		entry.action.behavior = RTE_ETH_FDIR_ACCEPT;

	if (fdir_conf.mode !=  RTE_FDIR_MODE_PERFECT_MAC_VLAN &&
	    fdir_conf.mode !=  RTE_FDIR_MODE_PERFECT_TUNNEL) {
		if (!strcmp(res->pf_vf, "pf"))
			entry.input.flow_ext.is_vf = 0;
		else if (!strncmp(res->pf_vf, "vf", 2)) {
			struct rte_eth_dev_info dev_info;

			memset(&dev_info, 0, sizeof(dev_info));
			rte_eth_dev_info_get(res->port_id, &dev_info);
			errno = 0;
			vf_id = strtoul(res->pf_vf + 2, &end, 10);
			if (errno != 0 || *end != '\0' ||
			    vf_id >= dev_info.max_vfs) {
				printf("invalid parameter %s.\n", res->pf_vf);
				return;
			}
			entry.input.flow_ext.is_vf = 1;
			entry.input.flow_ext.dst_id = (uint16_t)vf_id;
		} else {
			printf("invalid parameter %s.\n", res->pf_vf);
			return;
		}
	}

	/* set to report FD ID by default */
	entry.action.report_status = RTE_ETH_FDIR_REPORT_ID;
	entry.action.rx_queue = res->queue_id;
	entry.soft_id = res->fd_id_value;
	if (!strcmp(res->ops, "add"))
		ret = rte_eth_dev_filter_ctrl(res->port_id, RTE_ETH_FILTER_FDIR,
					     RTE_ETH_FILTER_ADD, &entry);
	else if (!strcmp(res->ops, "del"))
		ret = rte_eth_dev_filter_ctrl(res->port_id, RTE_ETH_FILTER_FDIR,
					     RTE_ETH_FILTER_DELETE, &entry);
	else
		ret = rte_eth_dev_filter_ctrl(res->port_id, RTE_ETH_FILTER_FDIR,
					     RTE_ETH_FILTER_UPDATE, &entry);
	if (ret < 0)
		printf("flow director programming error: (%s)\n",
			strerror(-ret));
}

cmdline_parse_token_string_t cmd_flow_director_filter =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
				 flow_director_filter, "flow_director_filter");
cmdline_parse_token_num_t cmd_flow_director_port_id =
	TOKEN_NUM_INITIALIZER(struct cmd_flow_director_result,
			      port_id, UINT16);
cmdline_parse_token_string_t cmd_flow_director_ops =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
				 ops, "add#del#update");
cmdline_parse_token_string_t cmd_flow_director_flow =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
				 flow, "flow");
cmdline_parse_token_string_t cmd_flow_director_flow_type =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
		flow_type, "ipv4-other#ipv4-frag#ipv4-tcp#ipv4-udp#ipv4-sctp#"
		"ipv6-other#ipv6-frag#ipv6-tcp#ipv6-udp#ipv6-sctp#l2_payload");
cmdline_parse_token_string_t cmd_flow_director_ether =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
				 ether, "ether");
cmdline_parse_token_num_t cmd_flow_director_ether_type =
	TOKEN_NUM_INITIALIZER(struct cmd_flow_director_result,
			      ether_type, UINT16);
cmdline_parse_token_string_t cmd_flow_director_src =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
				 src, "src");
cmdline_parse_token_ipaddr_t cmd_flow_director_ip_src =
	TOKEN_IPADDR_INITIALIZER(struct cmd_flow_director_result,
				 ip_src);
cmdline_parse_token_num_t cmd_flow_director_port_src =
	TOKEN_NUM_INITIALIZER(struct cmd_flow_director_result,
			      port_src, UINT16);
cmdline_parse_token_string_t cmd_flow_director_dst =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
				 dst, "dst");
cmdline_parse_token_ipaddr_t cmd_flow_director_ip_dst =
	TOKEN_IPADDR_INITIALIZER(struct cmd_flow_director_result,
				 ip_dst);
cmdline_parse_token_num_t cmd_flow_director_port_dst =
	TOKEN_NUM_INITIALIZER(struct cmd_flow_director_result,
			      port_dst, UINT16);
cmdline_parse_token_string_t cmd_flow_director_verify_tag =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
				  verify_tag, "verify_tag");
cmdline_parse_token_num_t cmd_flow_director_verify_tag_value =
	TOKEN_NUM_INITIALIZER(struct cmd_flow_director_result,
			      verify_tag_value, UINT32);
cmdline_parse_token_string_t cmd_flow_director_tos =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
				 tos, "tos");
cmdline_parse_token_num_t cmd_flow_director_tos_value =
	TOKEN_NUM_INITIALIZER(struct cmd_flow_director_result,
			      tos_value, UINT8);
cmdline_parse_token_string_t cmd_flow_director_proto =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
				 proto, "proto");
cmdline_parse_token_num_t cmd_flow_director_proto_value =
	TOKEN_NUM_INITIALIZER(struct cmd_flow_director_result,
			      proto_value, UINT8);
cmdline_parse_token_string_t cmd_flow_director_ttl =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
				 ttl, "ttl");
cmdline_parse_token_num_t cmd_flow_director_ttl_value =
	TOKEN_NUM_INITIALIZER(struct cmd_flow_director_result,
			      ttl_value, UINT8);
cmdline_parse_token_string_t cmd_flow_director_vlan =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
				 vlan, "vlan");
cmdline_parse_token_num_t cmd_flow_director_vlan_value =
	TOKEN_NUM_INITIALIZER(struct cmd_flow_director_result,
			      vlan_value, UINT16);
cmdline_parse_token_string_t cmd_flow_director_flexbytes =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
				 flexbytes, "flexbytes");
cmdline_parse_token_string_t cmd_flow_director_flexbytes_value =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
			      flexbytes_value, NULL);
cmdline_parse_token_string_t cmd_flow_director_drop =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
				 drop, "drop#fwd");
cmdline_parse_token_string_t cmd_flow_director_pf_vf =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
			      pf_vf, NULL);
cmdline_parse_token_string_t cmd_flow_director_queue =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
				 queue, "queue");
cmdline_parse_token_num_t cmd_flow_director_queue_id =
	TOKEN_NUM_INITIALIZER(struct cmd_flow_director_result,
			      queue_id, UINT16);
cmdline_parse_token_string_t cmd_flow_director_fd_id =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
				 fd_id, "fd_id");
cmdline_parse_token_num_t cmd_flow_director_fd_id_value =
	TOKEN_NUM_INITIALIZER(struct cmd_flow_director_result,
			      fd_id_value, UINT32);

cmdline_parse_token_string_t cmd_flow_director_mode =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
				 mode, "mode");
cmdline_parse_token_string_t cmd_flow_director_mode_ip =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
				 mode_value, "IP");
cmdline_parse_token_string_t cmd_flow_director_mode_mac_vlan =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
				 mode_value, "MAC-VLAN");
cmdline_parse_token_string_t cmd_flow_director_mode_tunnel =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
				 mode_value, "Tunnel");
cmdline_parse_token_string_t cmd_flow_director_mac =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
				 mac, "mac");
cmdline_parse_token_etheraddr_t cmd_flow_director_mac_addr =
	TOKEN_ETHERADDR_INITIALIZER(struct cmd_flow_director_result,
				    mac_addr);
cmdline_parse_token_string_t cmd_flow_director_tunnel =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
				 tunnel, "tunnel");
cmdline_parse_token_string_t cmd_flow_director_tunnel_type =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
				 tunnel_type, "NVGRE#VxLAN");
cmdline_parse_token_string_t cmd_flow_director_tunnel_id =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_result,
				 tunnel_id, "tunnel-id");
cmdline_parse_token_num_t cmd_flow_director_tunnel_id_value =
	TOKEN_NUM_INITIALIZER(struct cmd_flow_director_result,
			      tunnel_id_value, UINT32);

cmdline_parse_inst_t cmd_add_del_ip_flow_director = {
	.f = cmd_flow_director_filter_parsed,
	.data = NULL,
	.help_str = "flow_director_filter <port_id> mode IP add|del|update flow"
		" ipv4-other|ipv4-frag|ipv4-tcp|ipv4-udp|ipv4-sctp|"
		"ipv6-other|ipv6-frag|ipv6-tcp|ipv6-udp|ipv6-sctp|"
		"l2_payload src <src_ip> dst <dst_ip> tos <tos_value> "
		"proto <proto_value> ttl <ttl_value> vlan <vlan_value> "
		"flexbytes <flexbyte_values> drop|fw <pf_vf> queue <queue_id> "
		"fd_id <fd_id_value>: "
		"Add or delete an ip flow director entry on NIC",
	.tokens = {
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_filter,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_port_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mode,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mode_ip,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_ops,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flow,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flow_type,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_src,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_ip_src,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_dst,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_ip_dst,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_tos,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_tos_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_proto,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_proto_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_ttl,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_ttl_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_vlan,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_vlan_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flexbytes,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flexbytes_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_drop,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_pf_vf,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_queue,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_queue_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_fd_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_fd_id_value,
		NULL,
	},
};

cmdline_parse_inst_t cmd_add_del_udp_flow_director = {
	.f = cmd_flow_director_filter_parsed,
	.data = NULL,
	.help_str = "flow_director_filter ... : Add or delete an udp/tcp flow "
		"director entry on NIC",
	.tokens = {
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_filter,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_port_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mode,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mode_ip,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_ops,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flow,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flow_type,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_src,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_ip_src,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_port_src,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_dst,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_ip_dst,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_port_dst,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_tos,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_tos_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_ttl,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_ttl_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_vlan,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_vlan_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flexbytes,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flexbytes_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_drop,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_pf_vf,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_queue,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_queue_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_fd_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_fd_id_value,
		NULL,
	},
};

cmdline_parse_inst_t cmd_add_del_sctp_flow_director = {
	.f = cmd_flow_director_filter_parsed,
	.data = NULL,
	.help_str = "flow_director_filter ... : Add or delete a sctp flow "
		"director entry on NIC",
	.tokens = {
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_filter,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_port_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mode,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mode_ip,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_ops,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flow,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flow_type,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_src,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_ip_src,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_port_dst,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_dst,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_ip_dst,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_port_dst,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_verify_tag,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_verify_tag_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_tos,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_tos_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_ttl,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_ttl_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_vlan,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_vlan_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flexbytes,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flexbytes_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_drop,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_pf_vf,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_queue,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_queue_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_fd_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_fd_id_value,
		NULL,
	},
};

cmdline_parse_inst_t cmd_add_del_l2_flow_director = {
	.f = cmd_flow_director_filter_parsed,
	.data = NULL,
	.help_str = "flow_director_filter ... : Add or delete a L2 flow "
		"director entry on NIC",
	.tokens = {
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_filter,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_port_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mode,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mode_ip,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_ops,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flow,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flow_type,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_ether,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_ether_type,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flexbytes,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flexbytes_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_drop,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_pf_vf,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_queue,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_queue_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_fd_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_fd_id_value,
		NULL,
	},
};

cmdline_parse_inst_t cmd_add_del_mac_vlan_flow_director = {
	.f = cmd_flow_director_filter_parsed,
	.data = NULL,
	.help_str = "flow_director_filter ... : Add or delete a MAC VLAN flow "
		"director entry on NIC",
	.tokens = {
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_filter,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_port_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mode,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mode_mac_vlan,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_ops,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mac,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mac_addr,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_vlan,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_vlan_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flexbytes,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flexbytes_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_drop,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_queue,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_queue_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_fd_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_fd_id_value,
		NULL,
	},
};

cmdline_parse_inst_t cmd_add_del_tunnel_flow_director = {
	.f = cmd_flow_director_filter_parsed,
	.data = NULL,
	.help_str = "flow_director_filter ... : Add or delete a tunnel flow "
		"director entry on NIC",
	.tokens = {
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_filter,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_port_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mode,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mode_tunnel,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_ops,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mac,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mac_addr,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_vlan,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_vlan_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_tunnel,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_tunnel_type,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_tunnel_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_tunnel_id_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flexbytes,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flexbytes_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_drop,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_queue,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_queue_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_fd_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_fd_id_value,
		NULL,
	},
};

struct cmd_flush_flow_director_result {
	cmdline_fixed_string_t flush_flow_director;
	portid_t port_id;
};

cmdline_parse_token_string_t cmd_flush_flow_director_flush =
	TOKEN_STRING_INITIALIZER(struct cmd_flush_flow_director_result,
				 flush_flow_director, "flush_flow_director");
cmdline_parse_token_num_t cmd_flush_flow_director_port_id =
	TOKEN_NUM_INITIALIZER(struct cmd_flush_flow_director_result,
			      port_id, UINT16);

static void
cmd_flush_flow_director_parsed(void *parsed_result,
			  __attribute__((unused)) struct cmdline *cl,
			  __attribute__((unused)) void *data)
{
	struct cmd_flow_director_result *res =
		(struct cmd_flow_director_result *) parsed_result;
	int ret = 0;

	ret = rte_eth_dev_filter_supported(res->port_id, RTE_ETH_FILTER_FDIR);
	if (ret < 0) {
		printf("flow director is not supported on port %u.\n",
			res->port_id);
		return;
	}

	ret = rte_eth_dev_filter_ctrl(res->port_id, RTE_ETH_FILTER_FDIR,
			RTE_ETH_FILTER_FLUSH, NULL);
	if (ret < 0)
		printf("flow director table flushing error: (%s)\n",
			strerror(-ret));
}

cmdline_parse_inst_t cmd_flush_flow_director = {
	.f = cmd_flush_flow_director_parsed,
	.data = NULL,
	.help_str = "flush_flow_director <port_id>: "
		"Flush all flow director entries of a device on NIC",
	.tokens = {
		(cmdline_parse_token_hdr_t *)&cmd_flush_flow_director_flush,
		(cmdline_parse_token_hdr_t *)&cmd_flush_flow_director_port_id,
		NULL,
	},
};

/* *** deal with flow director mask *** */
struct cmd_flow_director_mask_result {
	cmdline_fixed_string_t flow_director_mask;
	portid_t port_id;
	cmdline_fixed_string_t mode;
	cmdline_fixed_string_t mode_value;
	cmdline_fixed_string_t vlan;
	uint16_t vlan_mask;
	cmdline_fixed_string_t src_mask;
	cmdline_ipaddr_t ipv4_src;
	cmdline_ipaddr_t ipv6_src;
	uint16_t port_src;
	cmdline_fixed_string_t dst_mask;
	cmdline_ipaddr_t ipv4_dst;
	cmdline_ipaddr_t ipv6_dst;
	uint16_t port_dst;
	cmdline_fixed_string_t mac;
	uint8_t mac_addr_byte_mask;
	cmdline_fixed_string_t tunnel_id;
	uint32_t tunnel_id_mask;
	cmdline_fixed_string_t tunnel_type;
	uint8_t tunnel_type_mask;
};

static void
cmd_flow_director_mask_parsed(void *parsed_result,
			  __attribute__((unused)) struct cmdline *cl,
			  __attribute__((unused)) void *data)
{
	struct cmd_flow_director_mask_result *res =
		(struct cmd_flow_director_mask_result *) parsed_result;
	struct rte_eth_fdir_masks *mask;
	struct rte_port *port;

	if (res->port_id > nb_ports) {
		printf("Invalid port, range is [0, %d]\n", nb_ports - 1);
		return;
	}

	port = &ports[res->port_id];
	/** Check if the port is not started **/
	if (port->port_status != RTE_PORT_STOPPED) {
		printf("Please stop port %d first\n", res->port_id);
		return;
	}

	mask = &port->dev_conf.fdir_conf.mask;

	if (fdir_conf.mode ==  RTE_FDIR_MODE_PERFECT_MAC_VLAN) {
		if (strcmp(res->mode_value, "MAC-VLAN")) {
			printf("Please set mode to MAC-VLAN.\n");
			return;
		}

		mask->vlan_tci_mask = rte_cpu_to_be_16(res->vlan_mask);
	} else if (fdir_conf.mode ==  RTE_FDIR_MODE_PERFECT_TUNNEL) {
		if (strcmp(res->mode_value, "Tunnel")) {
			printf("Please set mode to Tunnel.\n");
			return;
		}

		mask->vlan_tci_mask = rte_cpu_to_be_16(res->vlan_mask);
		mask->mac_addr_byte_mask = res->mac_addr_byte_mask;
		mask->tunnel_id_mask = rte_cpu_to_be_32(res->tunnel_id_mask);
		mask->tunnel_type_mask = res->tunnel_type_mask;
	} else {
		if (strcmp(res->mode_value, "IP")) {
			printf("Please set mode to IP.\n");
			return;
		}

		mask->vlan_tci_mask = rte_cpu_to_be_16(res->vlan_mask);
		IPV4_ADDR_TO_UINT(res->ipv4_src, mask->ipv4_mask.src_ip);
		IPV4_ADDR_TO_UINT(res->ipv4_dst, mask->ipv4_mask.dst_ip);
		IPV6_ADDR_TO_ARRAY(res->ipv6_src, mask->ipv6_mask.src_ip);
		IPV6_ADDR_TO_ARRAY(res->ipv6_dst, mask->ipv6_mask.dst_ip);
		mask->src_port_mask = rte_cpu_to_be_16(res->port_src);
		mask->dst_port_mask = rte_cpu_to_be_16(res->port_dst);
	}

	cmd_reconfig_device_queue(res->port_id, 1, 1);
}

cmdline_parse_token_string_t cmd_flow_director_mask =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_mask_result,
				 flow_director_mask, "flow_director_mask");
cmdline_parse_token_num_t cmd_flow_director_mask_port_id =
	TOKEN_NUM_INITIALIZER(struct cmd_flow_director_mask_result,
			      port_id, UINT16);
cmdline_parse_token_string_t cmd_flow_director_mask_vlan =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_mask_result,
				 vlan, "vlan");
cmdline_parse_token_num_t cmd_flow_director_mask_vlan_value =
	TOKEN_NUM_INITIALIZER(struct cmd_flow_director_mask_result,
			      vlan_mask, UINT16);
cmdline_parse_token_string_t cmd_flow_director_mask_src =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_mask_result,
				 src_mask, "src_mask");
cmdline_parse_token_ipaddr_t cmd_flow_director_mask_ipv4_src =
	TOKEN_IPADDR_INITIALIZER(struct cmd_flow_director_mask_result,
				 ipv4_src);
cmdline_parse_token_ipaddr_t cmd_flow_director_mask_ipv6_src =
	TOKEN_IPADDR_INITIALIZER(struct cmd_flow_director_mask_result,
				 ipv6_src);
cmdline_parse_token_num_t cmd_flow_director_mask_port_src =
	TOKEN_NUM_INITIALIZER(struct cmd_flow_director_mask_result,
			      port_src, UINT16);
cmdline_parse_token_string_t cmd_flow_director_mask_dst =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_mask_result,
				 dst_mask, "dst_mask");
cmdline_parse_token_ipaddr_t cmd_flow_director_mask_ipv4_dst =
	TOKEN_IPADDR_INITIALIZER(struct cmd_flow_director_mask_result,
				 ipv4_dst);
cmdline_parse_token_ipaddr_t cmd_flow_director_mask_ipv6_dst =
	TOKEN_IPADDR_INITIALIZER(struct cmd_flow_director_mask_result,
				 ipv6_dst);
cmdline_parse_token_num_t cmd_flow_director_mask_port_dst =
	TOKEN_NUM_INITIALIZER(struct cmd_flow_director_mask_result,
			      port_dst, UINT16);

cmdline_parse_token_string_t cmd_flow_director_mask_mode =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_mask_result,
				 mode, "mode");
cmdline_parse_token_string_t cmd_flow_director_mask_mode_ip =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_mask_result,
				 mode_value, "IP");
cmdline_parse_token_string_t cmd_flow_director_mask_mode_mac_vlan =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_mask_result,
				 mode_value, "MAC-VLAN");
cmdline_parse_token_string_t cmd_flow_director_mask_mode_tunnel =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_mask_result,
				 mode_value, "Tunnel");
cmdline_parse_token_string_t cmd_flow_director_mask_mac =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_mask_result,
				 mac, "mac");
cmdline_parse_token_num_t cmd_flow_director_mask_mac_value =
	TOKEN_NUM_INITIALIZER(struct cmd_flow_director_mask_result,
			      mac_addr_byte_mask, UINT8);
cmdline_parse_token_string_t cmd_flow_director_mask_tunnel_type =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_mask_result,
				 tunnel_type, "tunnel-type");
cmdline_parse_token_num_t cmd_flow_director_mask_tunnel_type_value =
	TOKEN_NUM_INITIALIZER(struct cmd_flow_director_mask_result,
			      tunnel_type_mask, UINT8);
cmdline_parse_token_string_t cmd_flow_director_mask_tunnel_id =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_mask_result,
				 tunnel_id, "tunnel-id");
cmdline_parse_token_num_t cmd_flow_director_mask_tunnel_id_value =
	TOKEN_NUM_INITIALIZER(struct cmd_flow_director_mask_result,
			      tunnel_id_mask, UINT32);

cmdline_parse_inst_t cmd_set_flow_director_ip_mask = {
	.f = cmd_flow_director_mask_parsed,
	.data = NULL,
	.help_str = "flow_director_mask ... : "
		"Set IP mode flow director's mask on NIC",
	.tokens = {
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_port_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_mode,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_mode_ip,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_vlan,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_vlan_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_src,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_ipv4_src,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_ipv6_src,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_port_src,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_dst,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_ipv4_dst,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_ipv6_dst,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_port_dst,
		NULL,
	},
};

cmdline_parse_inst_t cmd_set_flow_director_mac_vlan_mask = {
	.f = cmd_flow_director_mask_parsed,
	.data = NULL,
	.help_str = "flow_director_mask ... : Set MAC VLAN mode "
		"flow director's mask on NIC",
	.tokens = {
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_port_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_mode,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_mode_mac_vlan,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_vlan,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_vlan_value,
		NULL,
	},
};

cmdline_parse_inst_t cmd_set_flow_director_tunnel_mask = {
	.f = cmd_flow_director_mask_parsed,
	.data = NULL,
	.help_str = "flow_director_mask ... : Set tunnel mode "
		"flow director's mask on NIC",
	.tokens = {
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_port_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_mode,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_mode_tunnel,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_vlan,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_vlan_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_mac,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_mac_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_tunnel_type,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_tunnel_type_value,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_tunnel_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_mask_tunnel_id_value,
		NULL,
	},
};

/* *** deal with flow director mask on flexible payload *** */
struct cmd_flow_director_flex_mask_result {
	cmdline_fixed_string_t flow_director_flexmask;
	portid_t port_id;
	cmdline_fixed_string_t flow;
	cmdline_fixed_string_t flow_type;
	cmdline_fixed_string_t mask;
};

static void
cmd_flow_director_flex_mask_parsed(void *parsed_result,
			  __attribute__((unused)) struct cmdline *cl,
			  __attribute__((unused)) void *data)
{
	struct cmd_flow_director_flex_mask_result *res =
		(struct cmd_flow_director_flex_mask_result *) parsed_result;
	struct rte_eth_fdir_info fdir_info;
	struct rte_eth_fdir_flex_mask flex_mask;
	struct rte_port *port;
	uint32_t flow_type_mask;
	uint16_t i;
	int ret;

	if (res->port_id > nb_ports) {
		printf("Invalid port, range is [0, %d]\n", nb_ports - 1);
		return;
	}

	port = &ports[res->port_id];
	/** Check if the port is not started **/
	if (port->port_status != RTE_PORT_STOPPED) {
		printf("Please stop port %d first\n", res->port_id);
		return;
	}

	memset(&flex_mask, 0, sizeof(struct rte_eth_fdir_flex_mask));
	ret = parse_flexbytes(res->mask,
			flex_mask.mask,
			RTE_ETH_FDIR_MAX_FLEXLEN);
	if (ret < 0) {
		printf("error: Cannot parse mask input.\n");
		return;
	}

	memset(&fdir_info, 0, sizeof(fdir_info));
	ret = rte_eth_dev_filter_ctrl(res->port_id, RTE_ETH_FILTER_FDIR,
				RTE_ETH_FILTER_INFO, &fdir_info);
	if (ret < 0) {
		printf("Cannot get FDir filter info\n");
		return;
	}

	if (!strcmp(res->flow_type, "none")) {
		/* means don't specify the flow type */
		flex_mask.flow_type = RTE_ETH_FLOW_UNKNOWN;
		for (i = 0; i < RTE_ETH_FLOW_MAX; i++)
			memset(&port->dev_conf.fdir_conf.flex_conf.flex_mask[i],
			       0, sizeof(struct rte_eth_fdir_flex_mask));
		port->dev_conf.fdir_conf.flex_conf.nb_flexmasks = 1;
		rte_memcpy(&port->dev_conf.fdir_conf.flex_conf.flex_mask[0],
				 &flex_mask,
				 sizeof(struct rte_eth_fdir_flex_mask));
		cmd_reconfig_device_queue(res->port_id, 1, 1);
		return;
	}
	flow_type_mask = fdir_info.flow_types_mask[0];
	if (!strcmp(res->flow_type, "all")) {
		if (!flow_type_mask) {
			printf("No flow type supported\n");
			return;
		}
		for (i = RTE_ETH_FLOW_UNKNOWN; i < RTE_ETH_FLOW_MAX; i++) {
			if (flow_type_mask & (1 << i)) {
				flex_mask.flow_type = i;
				fdir_set_flex_mask(res->port_id, &flex_mask);
			}
		}
		cmd_reconfig_device_queue(res->port_id, 1, 1);
		return;
	}
	flex_mask.flow_type = str2flowtype(res->flow_type);
	if (!(flow_type_mask & (1 << flex_mask.flow_type))) {
		printf("Flow type %s not supported on port %d\n",
				res->flow_type, res->port_id);
		return;
	}
	fdir_set_flex_mask(res->port_id, &flex_mask);
	cmd_reconfig_device_queue(res->port_id, 1, 1);
}

cmdline_parse_token_string_t cmd_flow_director_flexmask =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_flex_mask_result,
				 flow_director_flexmask,
				 "flow_director_flex_mask");
cmdline_parse_token_num_t cmd_flow_director_flexmask_port_id =
	TOKEN_NUM_INITIALIZER(struct cmd_flow_director_flex_mask_result,
			      port_id, UINT16);
cmdline_parse_token_string_t cmd_flow_director_flexmask_flow =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_flex_mask_result,
				 flow, "flow");
cmdline_parse_token_string_t cmd_flow_director_flexmask_flow_type =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_flex_mask_result,
		flow_type, "none#ipv4-other#ipv4-frag#ipv4-tcp#ipv4-udp#ipv4-sctp#"
		"ipv6-other#ipv6-frag#ipv6-tcp#ipv6-udp#ipv6-sctp#l2_payload#all");
cmdline_parse_token_string_t cmd_flow_director_flexmask_mask =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_flex_mask_result,
				 mask, NULL);

cmdline_parse_inst_t cmd_set_flow_director_flex_mask = {
	.f = cmd_flow_director_flex_mask_parsed,
	.data = NULL,
	.help_str = "flow_director_flex_mask ... : "
		"Set flow director's flex mask on NIC",
	.tokens = {
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flexmask,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flexmask_port_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flexmask_flow,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flexmask_flow_type,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flexmask_mask,
		NULL,
	},
};

/* *** deal with flow director flexible payload configuration *** */
struct cmd_flow_director_flexpayload_result {
	cmdline_fixed_string_t flow_director_flexpayload;
	portid_t port_id;
	cmdline_fixed_string_t payload_layer;
	cmdline_fixed_string_t payload_cfg;
};

static inline int
parse_offsets(const char *q_arg, uint16_t *offsets, uint16_t max_num)
{
	char s[256];
	const char *p, *p0 = q_arg;
	char *end;
	unsigned long int_fld;
	char *str_fld[max_num];
	int i;
	unsigned size;
	int ret = -1;

	p = strchr(p0, '(');
	if (p == NULL)
		return -1;
	++p;
	p0 = strchr(p, ')');
	if (p0 == NULL)
		return -1;

	size = p0 - p;
	if (size >= sizeof(s))
		return -1;

	snprintf(s, sizeof(s), "%.*s", size, p);
	ret = rte_strsplit(s, sizeof(s), str_fld, max_num, ',');
	if (ret < 0 || ret > max_num)
		return -1;
	for (i = 0; i < ret; i++) {
		errno = 0;
		int_fld = strtoul(str_fld[i], &end, 0);
		if (errno != 0 || *end != '\0' || int_fld > UINT16_MAX)
			return -1;
		offsets[i] = (uint16_t)int_fld;
	}
	return ret;
}

static void
cmd_flow_director_flxpld_parsed(void *parsed_result,
			  __attribute__((unused)) struct cmdline *cl,
			  __attribute__((unused)) void *data)
{
	struct cmd_flow_director_flexpayload_result *res =
		(struct cmd_flow_director_flexpayload_result *) parsed_result;
	struct rte_eth_flex_payload_cfg flex_cfg;
	struct rte_port *port;
	int ret = 0;

	if (res->port_id > nb_ports) {
		printf("Invalid port, range is [0, %d]\n", nb_ports - 1);
		return;
	}

	port = &ports[res->port_id];
	/** Check if the port is not started **/
	if (port->port_status != RTE_PORT_STOPPED) {
		printf("Please stop port %d first\n", res->port_id);
		return;
	}

	memset(&flex_cfg, 0, sizeof(struct rte_eth_flex_payload_cfg));

	if (!strcmp(res->payload_layer, "raw"))
		flex_cfg.type = RTE_ETH_RAW_PAYLOAD;
	else if (!strcmp(res->payload_layer, "l2"))
		flex_cfg.type = RTE_ETH_L2_PAYLOAD;
	else if (!strcmp(res->payload_layer, "l3"))
		flex_cfg.type = RTE_ETH_L3_PAYLOAD;
	else if (!strcmp(res->payload_layer, "l4"))
		flex_cfg.type = RTE_ETH_L4_PAYLOAD;

	ret = parse_offsets(res->payload_cfg, flex_cfg.src_offset,
			    RTE_ETH_FDIR_MAX_FLEXLEN);
	if (ret < 0) {
		printf("error: Cannot parse flex payload input.\n");
		return;
	}

	fdir_set_flex_payload(res->port_id, &flex_cfg);
	cmd_reconfig_device_queue(res->port_id, 1, 1);
}

cmdline_parse_token_string_t cmd_flow_director_flexpayload =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_flexpayload_result,
				 flow_director_flexpayload,
				 "flow_director_flex_payload");
cmdline_parse_token_num_t cmd_flow_director_flexpayload_port_id =
	TOKEN_NUM_INITIALIZER(struct cmd_flow_director_flexpayload_result,
			      port_id, UINT16);
cmdline_parse_token_string_t cmd_flow_director_flexpayload_payload_layer =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_flexpayload_result,
				 payload_layer, "raw#l2#l3#l4");
cmdline_parse_token_string_t cmd_flow_director_flexpayload_payload_cfg =
	TOKEN_STRING_INITIALIZER(struct cmd_flow_director_flexpayload_result,
				 payload_cfg, NULL);

cmdline_parse_inst_t cmd_set_flow_director_flex_payload = {
	.f = cmd_flow_director_flxpld_parsed,
	.data = NULL,
	.help_str = "flow_director_flexpayload ... : "
		"Set flow director's flex payload on NIC",
	.tokens = {
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flexpayload,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flexpayload_port_id,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flexpayload_payload_layer,
		(cmdline_parse_token_hdr_t *)&cmd_flow_director_flexpayload_payload_cfg,
		NULL,
	},
};

/* Generic flow interface command. */
#ifdef RTE_BUILD_SHARED_LIB
cmdline_parse_inst_t cmd_flow;
#else
extern cmdline_parse_inst_t cmd_flow;
#endif

/* List of Flow Director instructions. */
cmdline_parse_ctx_t main_ctx[] = {
	(cmdline_parse_inst_t *)&cmd_ethertype_filter,
	(cmdline_parse_inst_t *)&cmd_2tuple_filter,
	(cmdline_parse_inst_t *)&cmd_5tuple_filter,
	(cmdline_parse_inst_t *)&cmd_flex_filter,
	(cmdline_parse_inst_t *)&cmd_add_del_ip_flow_director,
	(cmdline_parse_inst_t *)&cmd_add_del_udp_flow_director,
	(cmdline_parse_inst_t *)&cmd_add_del_sctp_flow_director,
	(cmdline_parse_inst_t *)&cmd_add_del_l2_flow_director,
	(cmdline_parse_inst_t *)&cmd_add_del_mac_vlan_flow_director,
	(cmdline_parse_inst_t *)&cmd_add_del_tunnel_flow_director,
	(cmdline_parse_inst_t *)&cmd_flush_flow_director,
	(cmdline_parse_inst_t *)&cmd_set_flow_director_ip_mask,
	(cmdline_parse_inst_t *)&cmd_set_flow_director_mac_vlan_mask,
	(cmdline_parse_inst_t *)&cmd_set_flow_director_tunnel_mask,
	(cmdline_parse_inst_t *)&cmd_set_flow_director_flex_mask,
	(cmdline_parse_inst_t *)&cmd_set_flow_director_flex_payload,
	(cmdline_parse_inst_t *)&cmd_flow,
	NULL,
};

void
fdir_set_flex_payload(portid_t port_id, struct rte_eth_flex_payload_cfg *cfg)
{
	struct rte_port *port;
	struct rte_eth_fdir_flex_conf *flex_conf;
	int i, idx = 0;

	port = &ports[port_id];
	flex_conf = &port->dev_conf.fdir_conf.flex_conf;
	for (i = 0; i < RTE_ETH_PAYLOAD_MAX; i++) {
		if (cfg->type == flex_conf->flex_set[i].type) {
			idx = i;
			break;
		}
	}
	if (i >= RTE_ETH_PAYLOAD_MAX) {
		if (flex_conf->nb_payloads < RTE_DIM(flex_conf->flex_set)) {
			idx = flex_conf->nb_payloads;
			flex_conf->nb_payloads++;
		} else {
			printf("The flex payload table is full. Can not set"
				" flex payload for type(%u).", cfg->type);
			return;
		}
	}
	rte_memcpy(&flex_conf->flex_set[idx],
			 cfg,
			 sizeof(struct rte_eth_flex_payload_cfg));

}

static void
cmd_reconfig_device_queue(portid_t id, uint8_t dev, uint8_t queue)
{
	if (id == (portid_t)RTE_PORT_ALL) {
		portid_t pid;

		RTE_ETH_FOREACH_DEV(pid) {
			/* check if need_reconfig has been set to 1 */
			if (ports[pid].need_reconfig == 0)
				ports[pid].need_reconfig = dev;
			/* check if need_reconfig_queues has been set to 1 */
			if (ports[pid].need_reconfig_queues == 0)
				ports[pid].need_reconfig_queues = queue;
		}
	} else if (!port_id_is_invalid(id, DISABLED_WARN)) {
		/* check if need_reconfig has been set to 1 */
		if (ports[id].need_reconfig == 0)
			ports[id].need_reconfig = dev;
		/* check if need_reconfig_queues has been set to 1 */
		if (ports[id].need_reconfig_queues == 0)
			ports[id].need_reconfig_queues = queue;
	}
}

CLICK_ENDDECLS