#!/usr/bin/env python
# -*- coding: utf-8 -*-

# sudo pip2 install ipy

import os
import argparse

from os.path import abspath, join
from common import *

DEF_RULE_PRIO = 1000
DEF_RULE_PERM = True
DEF_RULE_TIMEOUT = 0
DEF_ETH_PROTO = "0x800"

DEF_SW_DPID="of:000000223d4b0182"
DEF_SW_TABLE_ID=0

def add_of_rule_properties(sw_dpid, tableId=DEF_SW_TABLE_ID, priority=DEF_RULE_PRIO,
							permanent=DEF_RULE_PERM, timeout=DEF_RULE_TIMEOUT):
	rule_str  = "\t\t\t\"deviceId\": \"{}\",\n".format(sw_dpid)
	rule_str += "\t\t\t\"tableId\": {},\n".format(tableId)
	rule_str += "\t\t\t\"priority\": {},\n".format(priority)
	rule_str += "\t\t\t\"isPermanent\": \"{}\",\n".format(str(permanent).lower())
	rule_str += "\t\t\t\"timeout\": {},\n".format(timeout)
	return rule_str

def add_static_matching_criteria(sw_inport, eth_proto_hex=DEF_ETH_PROTO):
	rule_str  = "\t\t\t\t\t{\n"
	rule_str += "\t\t\t\t\t\t\"type\": \"IN_PORT\",\n"
	rule_str += "\t\t\t\t\t\t\"port\": {}\n".format(sw_inport)
	rule_str += "\t\t\t\t\t},\n"
	rule_str += "\t\t\t\t\t{\n"
	rule_str += "\t\t\t\t\t\t\"type\": \"ETH_TYPE\",\n"
	rule_str += "\t\t\t\t\t\t\"ethType\": \"{}\"\n".format(eth_proto_hex)
	rule_str += "\t\t\t\t\t},\n"
	return rule_str

def add_ipv4_matching_criteria(rule_map):
	rule_str = ""
	if not IPVF in rule_map:
		return rule_str

	for k, v in reversed(rule_map[IPVF].items()):
		# print("Proto: {} - {} --> {}".format(IPVF, k, v))
		rule_str += "\t\t\t\t\t{\n"
		if "proto" in k:
			rule_str += "\t\t\t\t\t\t\"type\": \"IP_PROTO\",\n"
			rule_str += "\t\t\t\t\t\t\"protocol\": {}\n".format(v)
		if ("src" in k) and (not "mask" in k):
			rule_str += "\t\t\t\t\t\t\"type\": \"IPV4_SRC\",\n"
			# IP with mask
			if "spec" in k:
				rule_str += "\t\t\t\t\t\t\"ip\": \"{}/{}\"\n".format(v, rule_map[IPVF][SRC_MASK])
			# IP only
			else:
				rule_str += "\t\t\t\t\t\t\"ip\": \"{}/32\"\n".format(v)
		if ("dst" in k) and (not "mask" in k):
			rule_str += "\t\t\t\t\t\t\"type\": \"IPV4_DST\",\n"
			# IP with mask
			if "spec" in k:
				rule_str += "\t\t\t\t\t\t\"ip\": \"{}/{}\"\n".format(v, rule_map[IPVF][DST_MASK])
			# IP only
			else:
				rule_str += "\t\t\t\t\t\t\"ip\": \"{}/32\"\n".format(v)
		rule_str += "\t\t\t\t\t},\n"
	return rule_str

def add_tcp_matching_criteria(rule_map):
	rule_str = ""
	if not TCP in rule_map:
		return rule_str

	for k, v in reversed(rule_map[TCP].items()):
		rule_str += "\t\t\t\t\t{\n"
		# print("Proto: {} - {} --> {}".format(TCP, k, v))
		if ("src" in k) and (not "mask" in k):
			rule_str += "\t\t\t\t\t\t\"type\": \"TCP_SRC\",\n"
			# Exact port match
			if "is" in k:
				rule_str += "\t\t\t\t\t\t\"tcpPort\": {}\n".format(v)
			# Wildcard port match
			else:
				raise RuntimeError("Wildcard TCP source port match is not supported")
		if ("dst" in k) and (not "mask" in k):
			rule_str += "\t\t\t\t\t\t\"type\": \"TCP_DST\",\n"
			# Exact port match
			if "is" in k:
				rule_str += "\t\t\t\t\t\t\"tcpPort\": {}\n".format(v)
			# Wildcard port match
			else:
				raise RuntimeError("Wildcard TCP destination port match is not supported")
		rule_str += "\t\t\t\t\t},\n"
	return rule_str

def add_udp_matching_criteria(rule_map):
	rule_str = ""
	if not UDP in rule_map:
		return rule_str
	rule_str += "\t\t\t\t\t{\n"
	for k, v in reversed(rule_map[UDP].items()):
		# print("Proto: {} - {} --> {}".format(UDP, k, v))
		if ("src" in k) and (not "mask" in k):
			rule_str += "\t\t\t\t\t\t\"type\": \"UDP_SRC\",\n"
			# Exact port match
			if "is" in k:
				rule_str += "\t\t\t\t\t\t\"udpPort\": {}\n".format(v)
			# Wildcard port match
			else:
				raise RuntimeError("Wildcard UDP source port match is not supported")
		if ("dst" in k) and (not "mask" in k):
			rule_str += "\t\t\t\t\t\t\"type\": \"UDP_DST\",\n"
			# Exact port match
			if "is" in k:
				rule_str += "\t\t\t\t\t\t\"udpPort\": {}\n".format(v)
			# Wildcard port match
			else:
				raise RuntimeError("Wildcard UDP destination port match is not supported")
	rule_str += "\t\t\t\t\t},\n"
	return rule_str

def add_dynamic_matching_criteria(rule_map):
	rule_str = ""

	rule_str += add_ipv4_matching_criteria(rule_map)
	rule_str += strip_last_occurence(add_tcp_matching_criteria(rule_map), ",")
	rule_str += strip_last_occurence(add_udp_matching_criteria(rule_map), ",")

	return rule_str

def add_actions(sw_outport, eth_src="ec:f4:bb:d5:ff:08", eth_dst="ec:f4:bb:d5:ff:0a"):
	rule_str  = "\t\t\t\"treatment\": {\n"
	rule_str += "\t\t\t\t\"instructions\": [\n"
	rule_str += "\t\t\t\t\t{\n"
	rule_str += "\t\t\t\t\t\t\"type\": \"OUTPUT\",\n"
	rule_str += "\t\t\t\t\t\t\"port\": {}\n".format(sw_outport)
	rule_str += "\t\t\t\t\t}\n"
	# Uncomment these lines if you wish to rewrite specific MAC addresses
	# rule_str += "\t\t\t\t\t{\n"
	# rule_str += "\t\t\t\t\t\t\"type\": \"L2MODIFICATION\",\n"
	# rule_str += "\t\t\t\t\t\t\"subtype\": \"ETH_SRC\",\n"
	# rule_str += "\t\t\t\t\t\t\"mac\": \"{}\"\n".format(eth_src)
	# rule_str += "\t\t\t\t\t},\n"
	# rule_str += "\t\t\t\t\t{\n"
	# rule_str += "\t\t\t\t\t\t\"type\": \"L2MODIFICATION\",\n"
	# rule_str += "\t\t\t\t\t\t\"subtype\": \"ETH_DST\",\n"
	# rule_str += "\t\t\t\t\t\t\"mac\": \"{}\"\n".format(eth_dst)
	# rule_str += "\t\t\t\t\t}\n"
	rule_str += "\t\t\t\t]\n"
	rule_str += "\t\t\t}\n"
	return rule_str

def dump_onos_of_rules(rule_list, outfile, sw_dpid, sw_inport, sw_outport, sw_tableid, verbose=False):
	"""
	Writes the rules of the input list into a file following ONOS's OpenFlow rule format.

	@param rule_list a list of rules to be dumped
	@param target_nic a target DPDK port ID
	@param target_queues_nb a target number of hardware queues
	@param outfile the output file where the data is written
	@param rule_count_instr if true, add a count instruction to rule actions
	"""

	assert rule_list, "Input data is NULL"
	assert outfile, "Filename is NULL"

	print("")

	with open(outfile, 'w') as f:
		curr_queue = 0
		rule_nb = 0

		rule_str = "{\n"
		rule_str += "\t\"flows\": [\n"

		for rule in rule_list:
			if verbose:
				print("Rule: {}".format(rule))

			rule_str += "\t\t{\n"
			rule_str += add_of_rule_properties(sw_dpid=sw_dpid, tableId=sw_tableid)

			# Append matches
			rule_str += "\t\t\t\"selector\": {\n"
			rule_str += "\t\t\t\t\"criteria\": [\n"
			rule_str += add_static_matching_criteria(sw_inport)
			rule_str += add_dynamic_matching_criteria(rule)
			rule_str += "\t\t\t\t]\n"
			rule_str += "\t\t\t},\n"

			# Append actions
			rule_str += add_actions(sw_outport)
			rule_str += "\t\t},\n"

			rule_nb += 1

		rule_str = strip_last_occurence(rule_str, ",")

		rule_str += "\t]\n"
		rule_str += "}\n"

		# Dump the rule
		f.write(rule_str + '\n')

	print("Dumped {} rules to file: {}".format(rule_nb, outfile))

def rule_list_to_file(rule_list, in_file, output_folder, sw_dpid, sw_inport, sw_outport, sw_tableid):
	outfile_pref = get_substring_until_delimiter(in_file, ".") + "_inport_{}_outport_{}.json".format(sw_inport, sw_outport)
	out_file = os.path.join("{}".format(os.path.abspath(output_folder)), outfile_pref)

	dump_onos_of_rules(rule_list, out_file, sw_dpid, sw_inport, sw_outport, sw_tableid)

def rule_gen_file(input_file_list, output_folder, sw_dpid, sw_inport, sw_outport, sw_tableid):
	for in_file in input_file_list:
		# Build the rules
		rule_list = parse_ipfilter(in_file)

		# Dump them to a file
		rule_list_to_file(rule_list, in_file, output_folder, sw_dpid, sw_inport, sw_outport, sw_tableid)

def rule_gen_random(output_folder, target_rules_nb, protocol, sw_dpid, sw_inport, sw_outport, sw_tableid):
	rule_list = []

	for i in xrange(target_rules_nb):
		rule = get_random_rule(i, protocol)
		rule_list.append(rule)

	# Dump them to a file
	in_file = "random_onos_of_rules_{}.json".format(target_rules_nb)
	rule_list_to_file(rule_list, in_file, output_folder, sw_dpid, sw_inport, sw_outport, sw_tableid)

###
### To translate rules from file:
### python click_to_onos_of_rules.py --strategy file --input-files test_click_rules --sw-dpid of:000000223d4b0182 --sw-inport 1 --sw-outport 3
###
### To generate random rules:
### python click_to_onos_of_rules.py --strategy random --target-rules-nb 4093 --sw-dpid of:000000223d4b0182 --sw-inport 1 --sw-outport 3 --protocol TCP
###

if __name__ == "__main__":
	parser = argparse.ArgumentParser("IPFilter/IPLookup rules to ONOS OpenFlow configurations")
	parser.add_argument("--strategy", type=str, default=STRATEGY_FILE, help="Strategy for rule generation. Can be [file, random]")
	parser.add_argument("--input-files", nargs='*', help="Set of space separated input files with IP-level Click rules")
	parser.add_argument("--output-folder", type=str, default=".", help="Output folder where rules will be stored")
	parser.add_argument("--sw-dpid", type=str, default=DEF_SW_DPID, help="The switch's Data Path ID (DPID)")
	parser.add_argument("--sw-inport", type=int, default=1, help="The switch's input port")
	parser.add_argument("--sw-outport", type=int, default=2, help="The switch's output port")
	parser.add_argument("--sw-tableid", type=int, default=DEF_SW_TABLE_ID, help="The switch's table ID")
	parser.add_argument("--target-rules-nb", type=int, default=CANNOT_SPECIFY_RULES_NB, help="For strategy random, you must specify how many random rules you need")
	parser.add_argument("--protocol", type=str, default=PROTO_RANDOM, help="Set IP protocol for random rule generation. Can be [TCP, UDP, RANDOM]")

	args = parser.parse_args()

	# Read input arguments
	strategy = args.strategy

	input_file_list = args.input_files
	if (not input_file_list) and (strategy == STRATEGY_FILE):
		raise RuntimeError("Specify a list of comma-separated input files with IPFilter/IPLookup) configuration")

	target_rules_nb = args.target_rules_nb
	if (target_rules_nb <= 0) and (strategy == STRATEGY_RAND):
		raise RuntimeError("Strategy random requires to pass the number of rules to generate. Use --target-rules-nb")
	print("Rule generation strategy is set to {}".format(strategy))

	output_folder = args.output_folder
	if not os.path.isdir(output_folder):
		raise RuntimeError("Invalid output folder: {}". format(output_folder))

	sw_dpid = args.sw_dpid
	if not sw_dpid:
		raise RuntimeError("Switch's DPID is missing.")

	sw_inport = args.sw_inport
	if sw_inport < 0:
		raise RuntimeError("Switch's input port must be non-negative.")

	sw_outport = args.sw_outport
	if sw_outport < 0:
		raise RuntimeError("Switch's output port must be non-negative.")

	sw_tableid = args.sw_tableid
	if sw_tableid < 0:
		raise RuntimeError("Switch's table ID must be non-negative.")

	protocol = args.protocol.lower()
	allowed_protos = [PROTO_TCP, PROTO_UDP, PROTO_RANDOM]
	if (protocol not in allowed_protos):
		raise RuntimeError("Specify a protocol type from this list: {}".format(allowed_protos))

	if strategy == STRATEGY_FILE:
		rule_gen_file(input_file_list, output_folder, sw_dpid, sw_inport, sw_outport, sw_tableid)
	else:
		rule_gen_random(output_folder, target_rules_nb, protocol, sw_dpid, sw_inport, sw_outport, sw_tableid)
