#!/usr/bin/python3
# -*- coding: utf-8 -*-

# sudo pip2 install ipy

import os
import argparse

from os.path import abspath, join
from common import *

OVS_OFCTL = "ovs-ofctl"
OVS_OFCTL_RULE_ADD = "add-flow"
OVS_OFCTL_PROTO_VER = "OpenFlow14"

TABLE = "table"
PRIORITY = "priority"
IN_PORT = "in_port"
OUT_PORT = "output"

ACTIONS = "actions"
ACTION_MOD_ETH_SRC = "mod_dl_src"
ACTION_MOD_ETH_DST = "mod_dl_dst"
ACTION_MOD_NW_SRC = "mod_nw_src"
ACTION_MOD_NW_DST = "mod_nw_dst"
ACTION_OUT_PORT = "output"
ACTION_JUMP = "goto_table"

DEF_BRIDGE = "ovsbr0"
DEF_TABLE = 0
DEF_RULE_POS = -1

DEF_PRIORITY = 0
DEF_IN_PORT = 1
DEF_OUT_PORT = 1
DEF_ETHER_SRC="00:00:00:00:00:a1"
DEF_ETHER_DST="00:00:00:00:00:a2"

IP_PROTO_TO_NB_MAP = {UDP: 17, TCP: 6}

def generate_jump_rule(target_bridge, target_table, input_port, ether_src, ether_dst, jump_table):
	"""
	When a table number > 0 is specified, one should install a jump
	rule to instruct OVS to forward input packets from table 0 to the
	desired table number.

	@param target_bridge a target OVS bridge name
	@param target_table a target flow table number to store the flow
	@param input_port the port number where packets are expected to arrive
	@param ether_src the source Ethernet address to match
	@param ether_dst the destination Ethernet address to match
	@param jump_table the next table to send packets to
	@return a jump rule

	Example: 
	"""

	rule_str = "{} -O {} {} {} ".format(OVS_OFCTL, OVS_OFCTL_PROTO_VER, OVS_OFCTL_RULE_ADD, target_bridge)
	rule_str += "\""

	rule_str += "{}={}, {}={}, ".format(TABLE, target_table, IN_PORT, input_port)
	rule_str += "{}={}, {}={}, {}={}, ".format(OVS_ETH_TYPE, ETH_TYPE_IP, OVS_ETH_SRC, ether_src, OVS_ETH_DST, ether_dst)
	rule_str += "{}={}:{}".format(ACTIONS, ACTION_JUMP, jump_table)

	rule_str += "\""

	return rule_str

def add_matching_criteria(rule_map):
	rule_str = ""
	for proto in [ETHERNET, IPVF, UDP, TCP]:
		if proto in rule_map:
			for k, v in reversed(rule_map[proto].items()):
				# print "K:{} - V:{}".format(k, v)
				rule_str += "{}={}, ".format(k, v)

	return rule_str

def add_actions(input_port, ether_src, ether_dst, output_port):
	out_port_str = "in_port" if output_port == input_port else str(output_port)
	# Ethernet addresses are swapped
	rule_str = "{}={}:{}, {}:{}, ".format(ACTIONS, ACTION_MOD_ETH_SRC, ether_dst, ACTION_MOD_ETH_DST, ether_src)
	rule_str += "{}:{}".format(ACTION_OUT_PORT, out_port_str)

	return rule_str

def dump_flow_rules(rule_list, target_bridge, target_table, priority, input_port, ether_src, ether_dst, output_port, outfile, verbose=False):
	"""
	Writes the rules of the input list into a file following DPDK's Flow API rule format.

	@param rule_list a list of rules to be dumped
	@param target_bridge a target DPDK port ID
	@param target_table a target flow table (i.e., group) to store the flow
	@param priority a desired rule priority
	@param input_port the port number where packets are expected to arrive
	@param ether_src the source Ethernet address to match
	@param ether_dst the destination Ethernet address to match
	@param output_port the port number where packets are expected to leave
	@param outfile the output file where the data is written
	@param verbose verbosity flag (defaults to false)
	"""

	assert rule_list, "Input data is NULL"
	assert outfile, "Filename is NULL"

	print("")

	with open(outfile, 'w') as f:
		rule_nb = 0

		# Generate an extra jump rule
		if target_table > 0:
			jump_rule = generate_jump_rule(target_bridge, 0, input_port, ether_src, ether_dst, target_table)
			if verbose:
				print("Rule: {}".format(jump_rule))
			f.write(jump_rule + '\n')

		for rule in rule_list:
			if verbose:
				print("Rule: {}".format(rule))

			rule_str = "{} -O {} {} {} ".format(OVS_OFCTL, OVS_OFCTL_PROTO_VER, OVS_OFCTL_RULE_ADD, target_bridge)
			rule_str += "\""

			if target_table >= 0:
				rule_str += TABLE + "=" + str(target_table) + ", "
			if priority >= 0:
				rule_str += PRIORITY + "=" + str(priority) + ", "
			rule_str += IN_PORT + "=" + str(input_port) + ", "

			# Append matches
			rule_str += add_matching_criteria(rule)

			# Append actions
			rule_str += add_actions(input_port, ether_src, ether_dst, output_port)

			rule_str += "\""

			print("OVS Flow rule #{0:>4}: {1}".format(rule_nb, rule_str))
			rule_nb += 1

			# Dump the rule
			f.write(rule_str + '\n')

	print("")
	print("Dumped {} rules to file: {}".format(rule_nb, outfile))

def rule_list_to_file(rule_list, in_file, output_folder, target_bridge, target_table, priority, input_port, ether_src, ether_dst, output_port):
	outfile_pref = get_substring_until_delimiter(in_file, ".") + "_table_{}.ovs".format(target_table)
	out_file = os.path.join("{}".format(os.path.abspath(output_folder)), outfile_pref)

	dump_flow_rules(rule_list, target_bridge, target_table, priority, input_port, ether_src, ether_dst, output_port, out_file)

def rule_gen_file(input_file_list, output_folder, target_bridge, target_table, priority, input_port, ether_src, ether_dst, output_port):
	for in_file in input_file_list:
		# Build the rules
		rule_list = parse_ipfilter(in_file, priority=priority, target="ovs")

		# Dump them to a file
		rule_list_to_file(rule_list, in_file, output_folder, target_bridge, target_table, priority, input_port, ether_src, ether_dst, output_port)

def rule_gen_random(output_folder, target_bridge, target_rules_nb, target_table, priority, input_port, ether_src, ether_dst, protocol, output_port, rule_pos):
	rule_list = []

	rules_nb = target_rules_nb
	start_pos = 0

	if (rule_pos >= rules_nb):
		rule_pos -= 1

	for i in xrange(start_pos, rules_nb):
		if (i == rule_pos):
			rule = get_desired_ovs_rule(priority, input_port, ether_src, ether_dst, protocol, output_port)
		else:
			rule = get_random_ovs_rule(priority, input_port, ether_src, ether_dst, protocol, output_port)
		rule_list.append(rule)

	# Dump them to a file
	in_file = "random_ovs_rules_{}.txt".format(target_rules_nb)
	rule_list_to_file(rule_list, in_file, output_folder, target_bridge, target_table, priority, input_port, ether_src, ether_dst, output_port)

"""
To translate rules from file:
    python3 click_to_openflow_ovs_rules.py --strategy file --input-files test_click_rules
To generate random rules:
    python3 click_to_openflow_ovs_rules.py --strategy random --target-bridge ovsdpdkbr0 \
        --target-rules-nb 10 --target-table 0 --input-port 1 \
        --ether-src b8:83:03:6f:43:38 --ether-dst b8:83:03:6f:43:40 \
        --protocol random --output-port 1
To generate random rules with a desired rule somewhere in the rule-set:
    python3 click_to_openflow_ovs_rules.py --strategy random --target-bridge ovsdpdkbr0 \
        --target-rules-nb 15 --target-table 1 --input-port 1 \
        --ether-src b8:83:03:6f:43:38 --ether-dst b8:83:03:6f:43:40 \
        --protocol random --output-port 1 --with-rule-at 7
"""

if __name__ == "__main__":
	parser = argparse.ArgumentParser("OVS-based OpenFlow rule generator")
	parser.add_argument("--strategy", type=str, default=STRATEGY_FILE, help="Strategy for rule generation. Can be [file, random]")
	parser.add_argument("--input-files", nargs='*', help="Set of space separated input files with IP-level Click rules")
	parser.add_argument("--output-folder", type=str, default=".", help="Output folder where rules will be stored")
	parser.add_argument("--target-bridge", type=str, default=DEF_BRIDGE, help="The OVS bridge name where the generated rules will be installed")
	parser.add_argument("--target-table", type=int, default=DEF_TABLE, help="The flow table number to store the rules")
	parser.add_argument("--target-rules-nb", type=int, default=CANNOT_SPECIFY_RULES_NB, help="For strategy random, you must specify how many random rules you need")
	parser.add_argument("--priority", type=int, default=DEF_PRIORITY, help="A desired rule priority")
	parser.add_argument("--input-port", type=int, default=DEF_IN_PORT, help="The port number where packets are expected to arrive")
	parser.add_argument("--output-port", type=int, default=DEF_OUT_PORT, help="The port number where packets are expected to leave.")
	parser.add_argument("--ether-src", type=str, default=DEF_ETHER_SRC, help="The source Ethernet address to match. Once matched, this address will be used as an action for ether_dst.")
	parser.add_argument("--ether-dst", type=str, default=DEF_ETHER_DST, help="The destination Ethernet address to match. Once matched, this address will be used as an action for ether_src.")
	parser.add_argument("--protocol", type=str, default=PROTO_RANDOM, help="Set IP protocol for random rule generation. Can be [TCP, UDP, RANDOM]")
	parser.add_argument("--with-rule-at", type=int, default=DEF_RULE_POS, help="Adds a desired rule at a designated position in the rule set")

	args = parser.parse_args()

	# Read input arguments
	strategy = args.strategy

	input_file_list = args.input_files
	if (not input_file_list) and (strategy == STRATEGY_FILE):
		raise RuntimeError("Specify a list of comma-separated input files with IPFilter/IPLookup configuration")

	target_rules_nb = args.target_rules_nb
	if (target_rules_nb <= 0) and (strategy == STRATEGY_RAND):
		raise RuntimeError("Strategy random requires to pass the number of rules to generate. Use --target-rules-nb")
	print("Rule generation strategy is set to {}\n".format(strategy))

	output_folder = args.output_folder
	if not os.path.isdir(output_folder):
		raise RuntimeError("Invalid output folder: {}". format(output_folder))

	target_bridge = args.target_bridge
	if not target_bridge:
		raise RuntimeError("A target bridge name must be provided.")

	target_table = args.target_table
	if target_table < 0:
		raise RuntimeError("A target flow table (i.e., group) number must be non-negative.")

	priority = args.priority
	if priority < 0:
		raise RuntimeError("A valid rule priority must be specified.")

	input_port = args.input_port
	if input_port < 0:
		raise RuntimeError("A valid input port number must be provided.")

	output_port = args.output_port
	if (output_port < 0) and (output_port != BOUNCE):
		raise RuntimeError("A valid output port number must be provided.")

	ether_src = args.ether_src
	if not check_mac(ether_src):
		raise RuntimeError("A valid source Ethernet address must be provided.")

	ether_dst = args.ether_dst
	if not check_mac(ether_dst):
		raise RuntimeError("A valid destination Ethernet address must be provided.")

	protocol = args.protocol.lower()
	allowed_protos = [PROTO_TCP, PROTO_UDP, PROTO_RANDOM]
	if (protocol not in allowed_protos):
		raise RuntimeError("Specify a protocol type from this list: {}".format(allowed_protos))

	rule_pos = args.with_rule_at
	if rule_pos > 0:
		rule_pos -= 1

	if strategy == STRATEGY_FILE:
		rule_gen_file(input_file_list, output_folder, target_bridge, target_table, priority, input_port, ether_src, ether_dst, output_port)
	else:
		rule_gen_random(output_folder, target_bridge, target_rules_nb, target_table, priority, input_port, ether_src, ether_dst, protocol, output_port, rule_pos)
