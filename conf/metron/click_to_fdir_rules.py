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

ALLOW = "allow"
DROP  = "drop"
DENY  = "deny"

IPFILTER = "IPFilter"
IPLOOKUP = "Lookup"
UNKNOWN  = "Unknown"

ENTRIES_ELEMENT = "entries"
TRAFFIC_CLASS_ELEMENT = "trafficClass"
OUTPUT_PORT_ELEMENT = "outputPort"
PRIORITY_ELEMENT = "priority"
ACTION_ELEMENT = "action"
ETHERNET   = "eth"
IPVF       = "ipv4"
IPVS       = "ipv6"
UDP        = "udp"
TCP        = "tcp"
PROTO_SPEC = "proto spec"
PROTO_MASK = "proto mask"
SRC_EXACT  = "src is"
SRC_SPEC   = "src spec"
SRC_MASK   = "src mask"
DST_EXACT  = "dst is"
DST_SPEC   = "dst spec"
DST_MASK   = "dst mask"
ACTION_Q   = "actions queue index"

def dump_data_to_file(rule_list, target_nic, target_queues_nb, outfile, verbose=False):
	"""
	Writes the contents of a string into a file.

	@param rule_list a list of rules to be dumped
	@param target_nic a target DPDK port ID
	@param target_queues_nb a target number of hardware queues
	@param outfile the output file where the data is written
	"""

	assert rule_list, "Input data is NULL"
	assert outfile, "Filename is NULL"

	with open(outfile, 'w') as f:
		curr_queue = 0
		rule_nb = 0

		for rule in rule_list:
			if verbose:
				print("Rule: {}".format(rule))

			rule_str = "flow create {} ingress pattern eth".format(target_nic)

			for proto in [IPVF, UDP, TCP]:
				if proto in rule:
					rule_str += " / {} ".format(proto.lower())
					for k, v in reversed(rule[proto].items()):
						rule_str += "{} {} ".format(k, v)
					# Remove the last ' '
					rule_str = rule_str[0 : len(rule_str) - 1]

			# Indicates end of matches
			rule_str += " / end "

			# Time to append the actions
			next_queue = (curr_queue % target_queues_nb)
			curr_queue += 1
			rule_str += "{} {} / end".format(ACTION_Q, next_queue)

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

def ipfilter_to_json(input_file):
	with open(input_file, "r") as f:
		rule_list = []
		rule_nb = 1
		for line in f:
			line = clean_line(line)

			classifier_type = type_of_classifier(line)
			assert classifier_type != UNKNOWN, "Click rule '" + line + "' does not follow the IPFilter/Lookup format"

			# Parse the action of this rule
			action = get_action(line, classifier_type)
			# The action defines the port
			if (classifier_type == IPFILTER and action == ALLOW):
				click_port = 0
			elif (classifier_type == IPFILTER and action == DENY):
				click_port = 1
			else:
				click_port = int(line.split(" ")[1])

			if (classifier_type == IPLOOKUP) and (not line.startswith("dst")):
				# Strip out the port as we already stored this information
				line = line.split(" ")[0]
				if "/32" in line:
					line = "dst host " + line
				else:
					line = "dst net " + line

			rule_map = {}
			rule_map[TRAFFIC_CLASS_ELEMENT] = line  # Stores the entire traffic class

			if "ip proto" in line:
				rule_map[IPVF] = {}
				rule_map[IPVF][PROTO_SPEC] = line.split("ip proto")[1].split()[0]
				rule_map[IPVF][PROTO_MASK] = "0x0"

			if "ip6" in line:
				print("Skipping unsupported IPv6 rule: {}".format(line))
				continue

			if any(i in line for i in ["src host", "src net", "dst host", "dst net"]):
				if not IPVF in rule_map:
					rule_map[IPVF] = {}

			if ("src host" in line):
				ip = line.split("src host")[1]
				if "/" in ip:
					rule_map[IPVF][SRC_EXACT] = ip.split("/")[0]
				else:
					rule_map[IPVF][SRC_EXACT] = ip
			if ("dst host" in line):
				ip = line.split("dst host")[1]
				if "/" in ip:
					rule_map[IPVF][DST_EXACT] = ip.split("/")[0]
				else:
					rule_map[IPVF][DST_EXACT] = ip

			if ("src net" in line):
				ip = line.split("src net")[1].split()[0]
				try:
					IP(ip)
				except:
					print("Source net IP '{}'' is not an IP address".format(ip))
					continue

				rule_map[IPVF][SRC_SPEC] = ip.split("/")[0]
				rule_map[IPVF][SRC_MASK] = str(IP(_prefixlenToNetmask(int(ip.split("/")[1]), 4)))
			if ("dst net" in line):
				ip = line.split("dst net")[1].split()[0]
				try:
					IP(ip)
				except:
					print("Destination net IP '{}'' is not an IP address".format(ip))
					continue

				rule_map[IPVF][DST_SPEC] = ip.split("/")[0]
				rule_map[IPVF][DST_MASK] = str(IP(_prefixlenToNetmask(int(ip.split("/")[1]), 4)))

			proto = TCP
			if ("src port" in line):
				if line.split("src port")[0].strip().endswith(UDP):
					rule_map[UDP] = {}
					proto = UDP
				elif line.split("src port")[0].strip().endswith(TCP):
					rule_map[TCP] = {}
				else:
					print("Assuming TCP src port for rule: {}".format(line))
					rule_map[TCP] = {}

				src_port = line.split("src port")[1].split()[0]
				try:
					assert src_port >= 0, "Negative source port in rule: {}".format(line)
				except:
					print("Negative source port: {}".format(src_port))
					continue

				rule_map[proto][SRC_EXACT] = src_port
			if ("dst port" in line):
				if line.split("dst port")[0].strip().endswith(UDP):
					proto = UDP
					if not UDP in rule_map:
						rule_map[UDP] = {}
				elif line.split("dst port")[0].strip().endswith(TCP):
					if not TCP in rule_map:
						rule_map[TCP] = {}
				else:
					print("Assuming TCP dst port for rule: {}".format(line))
					if not TCP in rule_map:
						rule_map[TCP] = {}

				dst_port = line.split("dst port")[1].split()[0]
				try:
					assert dst_port >= 0, "Negative destination port in rule: {}".format(line)
				except:
					print("Negative destination port: {}".format(dst_port))
					continue

				rule_map[proto][DST_EXACT] = dst_port

			# Build a map with the rule's info
			rule_map[OUTPUT_PORT_ELEMENT] = click_port
			rule_map[PRIORITY_ELEMENT] = rule_nb
			rule_map[ACTION_ELEMENT] = action

			# Add it to the list
			rule_list.append(rule_map)
			rule_nb += 1

	print("")

	return rule_list

### python click_to_fdir_rules.py --input-files test_click_rules --target-nic 0 --target-queues-nb 16

if __name__ == "__main__":
	parser = argparse.ArgumentParser("Click IPFilter/IPLookup rules to DPDK Flow rule configurations")
	parser.add_argument("--input-files", nargs='*', help="Set of space separated input files with IP-level Click rules")
	parser.add_argument("--output-folder", type=str, default=".", help="Output folder where rules will be stored")
	parser.add_argument("--target-nic", type=int, default=0, help="The DPDK port ID where the generated rules will be installed")
	parser.add_argument("--target-queues-nb", type=int, default=4, help="The number of hardware queues, to distribute the rules across")
	parser.add_argument("--iterative", action="store_true", help="Executes this script for 1 to target-queues-nb")

	args = parser.parse_args()

	# Read input arguments
	input_file_list = args.input_files
	if not input_file_list:
		raise RuntimeError("Specify a list of comma-separated input files with IPFilter/IPLookup) configuration")
	output_folder = args.output_folder
	if not os.path.isdir(output_folder):
		raise RuntimeError("Invalid output folder: {}". format(output_folder))
	target_nic = args.target_nic
	if target_nic < 0:
		raise RuntimeError("A target DPDK port ID is expected. Without this, we cannot generate NIC specific rules.")
	target_queues_nb = args.target_queues_nb
	if target_queues_nb <= 0:
		raise RuntimeError("A target number of hardware queues must be positive.")
	iterative = args.iterative
	start_queues_nb = 1 if iterative else target_queues_nb

	for in_file in input_file_list:
		# Build the rules
		data = ipfilter_to_json(in_file)

		# Generate one or multiple load balancing configurations
		for q in range(start_queues_nb, target_queues_nb + 1):
			out_file = os.path.join(
				"{}".format(os.path.abspath(output_folder)),
				get_substring_until_delimiter(in_file, ".") + "_port_{}_hw_queues_{}.fdir".format(target_nic, q)
			)
			dump_data_to_file(data, target_nic, q, out_file)
