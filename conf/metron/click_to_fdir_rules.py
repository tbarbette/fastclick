#!/usr/bin/env python
# -*- coding: utf-8 -*-

# sudo pip2 install ipy

import os
import sys
import codecs
import argparse

from os import listdir
from os.path import abspath, isfile, join, dirname
from IPy import IP, _prefixlenToNetmask
from random import randint

from common import *

STRATEGY_FILE = "file"
STRATEGY_RAND = "random"

PROTO_UDP = "udp"
PROTO_TCP = "tcp"
PROTO_RANDOM = "random"

ETHERNET_RULE_PREF = "ingress pattern eth"

INACTIVE = -1
NIC_INDEPENDENT = -1
DEF_QUEUES_NB = 4
TEMP_FILE="temp_click_rules"

def parse_protocols(rule_map):
	rule_str = ""
	for proto in [IPVF, UDP, TCP]:
		if proto in rule_map:
			rule_str += " / {} ".format(proto.lower())
			for k, v in reversed(rule_map[proto].items()):
				rule_str += "{} {} ".format(k, v)
			# Remove the last ' '
			rule_str = rule_str[0 : len(rule_str) - 1]

	return rule_str

def dump_flow_director(rule_list, target_nic, target_queues_nb, outfile, rule_count_instr, verbose=False):
	"""
	Writes the contents of a string into a file.

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

		for rule in rule_list:
			if verbose:
				print("Rule: {}".format(rule))

			rule_str = ""
			if target_nic >= 0:
				rule_str = "flow create {} {}".format(target_nic, ETHERNET_RULE_PREF)
			else:
				rule_str = ETHERNET_RULE_PREF

			rule_str += parse_protocols(rule)

			# Indicates end of matches
			rule_str += " / end "

			# Time to append the actions
			rule_str += "actions "
			# Queue dispatching
			next_queue = (curr_queue % target_queues_nb)
			curr_queue += 1
			rule_str += "{} {} / ".format(ACTION_Q, next_queue)
			# Monitoring
			if rule_count_instr:
				rule_str += "{} / ".format(ACTION_CNT)
			rule_str += "end "

			print("DPDK Flow rule #{0:>4}: {1}".format(rule_nb, rule_str))
			rule_nb += 1

			# Dump the rule
			f.write(rule_str + '\n')

	print("")
	print("Dumped {} rules to file: {}".format(rule_nb, outfile))

def get_substring_until_delimiter(string, delimiter):
	if string.rfind(delimiter) > 1:
		return string.split(delimiter)[0]
	return string

def clean_line(line):
	return line.replace(",", "").strip()

def get_action(rule, classifier_type):
	if classifier_type == IPFILTER:
		if rule.startswith(ALLOW):
			return ALLOW
		elif rule.startswith(DROP) or rule.startswith(DENY):
			return DENY
		else:
			raise RuntimeError("Invalid action for rule: " + rule)
	else:
		return ALLOW

def get_click_port(rule, classifier_type, action):
	# The action defines the port
	if (classifier_type == IPFILTER and action == ALLOW):
		return 0
	elif (classifier_type == IPFILTER and action == DENY):
		return 1
	else:
		return int(rule.split(" ")[1])

def get_proto_str(rule):
	if UDP in rule:
		return UDP
	elif TCP in rule:
		return TCP
	return ""

def get_pattern_value(rule, pattern, verbose=False):
	assert pattern, "Cannot extract value from non-existent pattern"
	rest = clean_line(rule.split(pattern)[1])
	value = rest.split(" ")[0]

	if (verbose):
		print("Pattern: {} ---> Value: {}".format(pattern, value))

	return value

def check_ip(ip):
	try:
		IP(ip)
	except:
		raise RuntimeError("IP '{}'' is not an IP address".format(ip))

def check_port(port):
	assert port >= 0, "Negative port: {}".format(port)

def type_of_classifier(rule):
	if rule.startswith(ALLOW) or rule.startswith(DROP) or rule.startswith(DENY):
		return IPFILTER

	components = rule.split(" ")
	if len(components) != 2:
		print("Rule '{}'' has 3 tokens".format(rule))
		return UNKNOWN

	try:
		IP(components[0])
	except:
		print("First word in rule '{}'' is not an IP address".format(rule))
		return UNKNOWN

	if not components[1].isdigit():
		print("Second word in rule '{}' is not a Click port".format(rule))
		return UNKNOWN

	return IPLOOKUP

def parse_ipfilter(input_file):
	with open(input_file, "r") as f:
		rule_list = []
		rule_nb = 1
		for line in f:
			line = clean_line(line)

			classifier_type = type_of_classifier(line)
			assert classifier_type != UNKNOWN, "Click rule '" + line + "' does not follow the IPFilter/Lookup format"

			# Parse the action of this rule
			action = get_action(line, classifier_type)

			# Get the right Click port
			click_port = get_click_port(line, classifier_type, action)

			if (classifier_type == IPLOOKUP) and (not line.startswith("dst")):
				# Strip out the port as we already stored this information
				line = line.split(" ")[0]
				if "/32" in line:
					line = "dst host " + line
				else:
					line = "dst net " + line

			rule_map = {}
			rule_map[TRAFFIC_CLASS_ELEMENT] = line  # Keep the entire rule for debugging purposes

			if "ip proto" in line:
				rule_map[IPVF] = {}
				rule_map[IPVF][PROTO_EXACT] = line.split("ip proto")[1].split()[0]

			if "ip6" in line:
				print("Skipping unsupported IPv6 rule: {}".format(line))
				continue

			if any(i in line for i in ["src host", "src net", "dst host", "dst net"]):
				if not IPVF in rule_map:
					rule_map[IPVF] = {}

			if ("src host" in line):
				ip = get_pattern_value(line, "src host")
				if "/" in ip:
					rule_map[IPVF][SRC_EXACT] = ip.split("/")[0]
				else:
					rule_map[IPVF][SRC_EXACT] = ip
			if ("dst host" in line):
				ip = get_pattern_value(line, "dst host")
				if "/" in ip:
					rule_map[IPVF][DST_EXACT] = ip.split("/")[0]
				else:
					rule_map[IPVF][DST_EXACT] = ip

			if ("src net" in line):
				ip = get_pattern_value(line, "src net")
				check_ip(ip)
				rule_map[IPVF][SRC_SPEC] = ip.split("/")[0]
				rule_map[IPVF][SRC_MASK] = str(IP(_prefixlenToNetmask(int(ip.split("/")[1]), 4)))
			if ("dst net" in line):
				ip = get_pattern_value(line, "dst net")
				check_ip(ip)
				rule_map[IPVF][DST_SPEC] = ip.split("/")[0]
				rule_map[IPVF][DST_MASK] = str(IP(_prefixlenToNetmask(int(ip.split("/")[1]), 4)))

			proto = get_proto_str(line)
			if proto:
				rule_map[proto] = {}
				if ("src port" in line):
					src_port = get_pattern_value(line, "src port")
					check_port(src_port)
					rule_map[proto][SRC_EXACT] = src_port
				if ("dst port" in line):
					dst_port = get_pattern_value(line, "dst port")
					check_port(dst_port)
					rule_map[proto][DST_EXACT] = dst_port

			# Build a map with the rule's info
			rule_map[OUTPUT_PORT_ELEMENT] = click_port
			rule_map[PRIORITY_ELEMENT] = rule_nb
			rule_map[ACTION_ELEMENT] = action

			# Add it to the list and continue
			rule_list.append(rule_map)
			rule_nb += 1

	return rule_list

def get_random_ipv4_address():
	return ".".join(str(randint(0, 255)) for _ in range(4))

def get_ip_proto_str(protocol):
	proto_str = ""
	if protocol == PROTO_RANDOM:
		proto_str = UDP if randint(0, 1) == 0 else TCP
	else:
		proto_str = UDP if protocol == PROTO_UDP else TCP
	return proto_str

def ip_proto_str_to_int(proto_str):
	if proto_str == TCP:
		return 6
	elif proto_str == UDP:
		return 17
	return -1

def get_random_port():
	return randint(0, 65535)

def get_random_rule(rule_nb, protocol):
	proto = get_ip_proto_str(protocol)

	rule_map = {}
	rule_map[IPVF] = {}
	if proto:
		rule_map[IPVF][PROTO_EXACT] = ip_proto_str_to_int(proto)
	rule_map[IPVF][SRC_EXACT] = get_random_ipv4_address()
	rule_map[IPVF][DST_EXACT] = get_random_ipv4_address()
	if proto:
		rule_map[proto] = {}
		rule_map[proto][SRC_EXACT] = str(get_random_port())
		rule_map[proto][DST_EXACT] = str(get_random_port())
	rule_map[OUTPUT_PORT_ELEMENT] = 0
	rule_map[PRIORITY_ELEMENT] = rule_nb
	rule_map[ACTION_ELEMENT] = ALLOW

	return rule_map

def rule_list_to_file(rule_list, in_file, output_folder, target_nic, start_queues_nb, target_queues_nb, rule_count_instr):
	# Generate one or multiple load balancing configurations
	for q in range(start_queues_nb, target_queues_nb + 1):
		outfile_pref = get_substring_until_delimiter(in_file, ".") + "_hw_queues_{}.fdir".format(q)
		out_file = os.path.join("{}".format(os.path.abspath(output_folder)), outfile_pref)

		dump_flow_director(rule_list, target_nic, q, out_file, rule_count_instr)

def rule_gen_file(input_file_list, output_folder, target_nic, start_queues_nb, target_queues_nb, rule_count_instr=False):
	for in_file in input_file_list:
		# Build the rules
		rule_list = parse_ipfilter(in_file)

		# Dump them to a file
		rule_list_to_file(rule_list, in_file, output_folder, target_nic, start_queues_nb, target_queues_nb, rule_count_instr)

def rule_gen_random(output_folder, target_nic, target_rules_nb, start_queues_nb, target_queues_nb, protocol, rule_count_instr=False):
	rule_list = []

	for i in xrange(target_rules_nb):
		rule = get_random_rule(i, protocol)
		rule_list.append(rule)

	# Dump them to a file
	in_file = "random_flow_dir_rules_{}.txt".format(target_rules_nb)
	rule_list_to_file(rule_list, in_file, output_folder, target_nic, start_queues_nb, target_queues_nb, rule_count_instr)

###
### To translate rules from file:
### python click_to_fdir_rules.py --strategy file --input-files test_click_rules --target-queues-nb 16
###
### To generate random rules:
### python click_to_fdir_rules.py --strategy random --target-rules-nb 1000 --target-queues-nb 16 --rule-count --protocol UDP
###

if __name__ == "__main__":
	parser = argparse.ArgumentParser("Click IPFilter/IPLookup rules to DPDK Flow rule configurations")
	parser.add_argument("--strategy", type=str, default=STRATEGY_FILE, help="Strategy for rule generation. Can be [file, random]")
	parser.add_argument("--input-files", nargs='*', help="Set of space separated input files with IP-level Click rules")
	parser.add_argument("--output-folder", type=str, default=".", help="Output folder where rules will be stored")
	parser.add_argument("--target-nic", type=int, default=NIC_INDEPENDENT, help="The DPDK port ID where the generated rules will be installed or -1 for NIC independent rules")
	parser.add_argument("--target-rules-nb", type=int, default=INACTIVE, help="For strategy random, you must specify how many random rules you need")
	parser.add_argument("--target-queues-nb", type=int, default=DEF_QUEUES_NB, help="The number of hardware queues, to distribute the rules across")
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

	protocol = args.protocol.lower()
	allowed_protos = [PROTO_TCP, PROTO_UDP, PROTO_RANDOM]
	if (protocol not in allowed_protos):
		raise RuntimeError("Specify a protocol type from this list: {}".format(allowed_protos))

	rule_count_instr = args.rule_count

	iterative = args.iterative
	start_queues_nb = 1 if iterative else target_queues_nb

	if strategy == STRATEGY_FILE:
		rule_gen_file(input_file_list, output_folder, target_nic, start_queues_nb, target_queues_nb, rule_count_instr)
	else:
		rule_gen_random(output_folder, target_nic, target_rules_nb, start_queues_nb, target_queues_nb, protocol, rule_count_instr)
