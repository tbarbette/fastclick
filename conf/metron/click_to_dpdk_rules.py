#!/usr/bin/env python
# -*- coding: utf-8 -*-

# sudo pip2 install ipy

import os
import argparse

from os.path import abspath, join
from common import *

FD_CREATE = "flow create"
FD_ETHERNET_RULE_PREF = "pattern eth"

GROUP = "group"
INGRESS = "ingress"
EGRESS = "egress"
TRANSFER = "transfer"

ACTIONS = "actions"
ACTION_QUEUE = "queue index"
ACTION_COUNT = "count"
ACTION_JUMP = "jump"

NIC_INDEPENDENT = -1
DEF_QUEUES_NB = 4
DEF_GROUP_NB = -1

def generate_jump_rule(target_nic, target_group_nb, rule_count_instr):
	"""
	When a group number > 0 is specified, one should install a jump
	rule in the NIC to instruct the hardware to forward input packets
	from group 0 to the desired group number.

	@param target_nic a target DPDK port ID
	@param target_group_nb a target flow table (i.e., group) to store the flow
	@param rule_count_instr if true, add a count instruction to rule actions
	@return a jump rule

	Example: group 0 ingress pattern eth / end actions jump group 1 / end
	"""

	rule_str = ""

	if target_nic >= 0:
		rule_str = "{} {} ".format(FD_CREATE, target_nic)

	rule_str += GROUP + " 0 " + INGRESS + " " + FD_ETHERNET_RULE_PREF + " / end "
	rule_str += ACTIONS + " " + ACTION_JUMP + " " + GROUP + " " + str(target_group_nb) + " / "

	if rule_count_instr:
		rule_str += "{} / ".format(ACTION_COUNT)
	rule_str += "end "

	return rule_str

def add_matching_criteria(rule_map):
	rule_str = ""
	for proto in [IPVF, UDP, TCP]:
		if proto in rule_map:
			rule_str += " / {} ".format(proto.lower())
			for k, v in reversed(rule_map[proto].items()):
				rule_str += "{} {} ".format(k, v)
			# Remove the last ' '
			rule_str = rule_str[0 : len(rule_str) - 1]

	# Indicates end of matches
	rule_str += " / end "

	return rule_str

def add_actions(curr_queue, target_queues_nb, rule_count_instr):
	rule_str = ACTIONS + " "
	# Queue dispatching
	next_queue = (curr_queue % target_queues_nb)
	rule_str += "{} {} / ".format(ACTION_QUEUE, next_queue)
	# Counting
	if rule_count_instr:
		rule_str += "{} / ".format(ACTION_COUNT)
	rule_str += "end "
	return rule_str

def dump_flow_rules(rule_list, target_nic, target_queues_nb, target_group_nb, outfile, rule_count_instr, verbose=False):
	"""
	Writes the rules of the input list into a file following DPDK's Flow API rule format.

	@param rule_list a list of rules to be dumped
	@param target_nic a target DPDK port ID
	@param target_queues_nb a target number of hardware queues
	@param target_group_nb a target flow table (i.e., group) to store the flow
	@param outfile the output file where the data is written
	@param rule_count_instr if true, add a count instruction to rule actions
	"""

	assert rule_list, "Input data is NULL"
	assert outfile, "Filename is NULL"

	print("")

	with open(outfile, 'w') as f:
		curr_queue = 0
		rule_nb = 0

		# Generate an extra jump rule
		if target_group_nb > 0:
			jump_rule = generate_jump_rule(target_nic, target_group_nb, rule_count_instr)
			if verbose:
				print("Rule: {}".format(jump_rule))
			f.write(jump_rule + '\n')

		for rule in rule_list:
			if verbose:
				print("Rule: {}".format(rule))

			rule_str = ""
			if target_nic >= 0:
				rule_str = "{} {} ".format(FD_CREATE, target_nic)

			if target_group_nb >= 0:
				rule_str += GROUP + " " + str(target_group_nb) + " " + INGRESS + " " + FD_ETHERNET_RULE_PREF

			# Append matches
			rule_str += add_matching_criteria(rule)

			# Append actions
			rule_str += add_actions(curr_queue, target_queues_nb, rule_count_instr)
			curr_queue += 1

			print("DPDK Flow rule #{0:>4}: {1}".format(rule_nb, rule_str))
			rule_nb += 1

			# Dump the rule
			f.write(rule_str + '\n')

	print("")
	print("Dumped {} rules to file: {}".format(rule_nb, outfile))

def rule_list_to_file(rule_list, in_file, output_folder, target_nic, start_queues_nb, target_queues_nb, target_group_nb, rule_count_instr):
	# Generate one or multiple load balancing configurations
	for q in range(start_queues_nb, target_queues_nb + 1):
		outfile_pref = get_substring_until_delimiter(in_file, ".") + "_group_{}_hw_queues_{}.dpdk".format(target_group_nb, q)
		out_file = os.path.join("{}".format(os.path.abspath(output_folder)), outfile_pref)

		dump_flow_rules(rule_list, target_nic, q, target_group_nb, out_file, rule_count_instr)

def rule_gen_file(input_file_list, output_folder, target_nic, start_queues_nb, target_queues_nb, target_group_nb, rule_count_instr=False):
	for in_file in input_file_list:
		# Build the rules
		rule_list = parse_ipfilter(in_file)

		# Dump them to a file
		rule_list_to_file(rule_list, in_file, output_folder, target_nic, start_queues_nb, target_queues_nb, target_group_nb, rule_count_instr)

def rule_gen_random(output_folder, target_nic, target_rules_nb, start_queues_nb, target_queues_nb, target_group_nb, protocol, rule_count_instr=False):
	rule_list = []

	rules_nb = target_rules_nb

	# If group > 0, we will generate an extra jump rule
	if target_group_nb > 0:
		rules_nb -= 1

	for i in xrange(rules_nb):
		rule = get_random_rule(i, protocol)
		rule_list.append(rule)

	# Dump them to a file
	in_file = "random_dpdk_rules_{}.txt".format(target_rules_nb)
	rule_list_to_file(rule_list, in_file, output_folder, target_nic, start_queues_nb, target_queues_nb, target_group_nb, rule_count_instr)

###
### To translate rules from file:
###   python click_to_dpdk_rules.py --strategy file --input-files test_click_rules --target-queues-nb 16
###
### To generate random rules:
### |-> NIC independent rule set with random transport protocol:
###     python click_to_dpdk_rules.py --strategy random --target-rules-nb 48000 --target-queues-nb 1 --target-group-nb 0 --rule-count
### |-> NIC dependent (i.e., NIC 0) with TCP as transport protocol
###     python click_to_dpdk_rules.py --strategy random --target-nic 0 --target-rules-nb 65536 --target-queues-nb 1 --target-group-nb 1 --rule-count --protocol TCP
###

if __name__ == "__main__":
	parser = argparse.ArgumentParser("Click IPFilter/IPLookup rules to DPDK Flow rule configurations")
	parser.add_argument("--strategy", type=str, default=STRATEGY_FILE, help="Strategy for rule generation. Can be [file, random]")
	parser.add_argument("--input-files", nargs='*', help="Set of space separated input files with IP-level Click rules")
	parser.add_argument("--output-folder", type=str, default=".", help="Output folder where rules will be stored")
	parser.add_argument("--target-nic", type=int, default=NIC_INDEPENDENT, help="The DPDK port ID where the generated rules will be installed or -1 for NIC independent rules")
	parser.add_argument("--target-rules-nb", type=int, default=CANNOT_SPECIFY_RULES_NB, help="For strategy random, you must specify how many random rules you need")
	parser.add_argument("--target-queues-nb", type=int, default=DEF_QUEUES_NB, help="The number of hardware queues, to distribute the rules across")
	parser.add_argument("--target-group-nb", type=int, default=DEF_GROUP_NB, help="The flow table (i.e., group) number to store the rules")
	parser.add_argument("--protocol", type=str, default=PROTO_RANDOM, help="Set IP protocol for random rule generation. Can be [TCP, UDP, RANDOM]")
	parser.add_argument("--rule-count", action="store_true", help="Adds rule counter instructions to rules")
	parser.add_argument("--iterative", action="store_true", help="Executes this script for 1 to target-queues-nb")

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

	target_nic = args.target_nic
	if target_nic == NIC_INDEPENDENT:
		print("Parameter --target-nic is set to -1. Rule generation will be NIC independent!")

	target_queues_nb = args.target_queues_nb
	if target_queues_nb <= 0:
		raise RuntimeError("A target number of hardware queues must be positive.")

	target_group_nb = args.target_group_nb
	if target_group_nb < 0:
		raise RuntimeError("A target flow table (i.e., group) number must be non-negative.")

	protocol = args.protocol.lower()
	allowed_protos = [PROTO_TCP, PROTO_UDP, PROTO_RANDOM]
	if (protocol not in allowed_protos):
		raise RuntimeError("Specify a protocol type from this list: {}".format(allowed_protos))

	rule_count_instr = args.rule_count

	iterative = args.iterative
	start_queues_nb = 1 if iterative else target_queues_nb

	if strategy == STRATEGY_FILE:
		rule_gen_file(input_file_list, output_folder, target_nic, start_queues_nb, target_queues_nb, target_group_nb, rule_count_instr)
	else:
		rule_gen_random(output_folder, target_nic, target_rules_nb, start_queues_nb, target_queues_nb, target_group_nb, protocol, rule_count_instr)
