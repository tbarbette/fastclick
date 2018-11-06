#!/usr/bin/env python
# -*- coding: utf-8 -*-

import os
import sys
import json
import codecs
import logging
import argparse

from os import listdir
from os.path import abspath, isfile, join, dirname
from IPy import IP

from common import *

RULE_FORMAT_CLICK = "click"
RULE_FORMAT_FD = "flowdir"

def except_hook(*caught):
	"""
	Exception handler that uses the logger instead of regular stdout functions.
	"""

	print("Exception: {}".format(caught))
	raise RuntimeError

# Custom handling of all exceptions
sys.excepthook = except_hook

def dump_string_to_file(data, outfile):
	"""
	Writes the contents of a string into a file.

	@param data the string to be written into the file
	@param outfile the output file where the data is written
	"""

	assert data, "String is NULL"
	assert outfile, "Filename is NULL"

	with open(outfile, 'w') as f:
			f.write(data + '\n')

	print("Output file: {}".format(outfile))

def get_files_from_folder(folder):
	file_list = []
	for f in listdir(input_folder):
		filename = join(input_folder, f)
		if isfile(filename):
			file_list.append(filename)

	return file_list

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
	elif classifier_type == IPLOOKUP:
		return ALLOW
	elif classifier_type == FLOW_DIR:
		if DROP in rule:
			return DENY
		return ALLOW
	else:
		return ""

def type_of_classifier(rule):
	if rule.startswith(ALLOW) or rule.startswith(DROP) or rule.startswith(DENY):
		return IPFILTER

	if rule.startswith(FD_CREATE) or rule.startswith(INGRESS) or rule.startswith(EGRESS) or rule.startswith(TRANSFER):
		return FLOW_DIR

	# From now on, could be IPLookup or Unknown
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

def parse_ip(layer, ip_dir):
	tokens = layer.split(" ")
	if ip_dir + " is" in layer:
		ip = IP(layer.split(ip_dir + " is")[1].strip().split(" ")[0])
		return ip_dir + " host " + ip.strCompressed(wantprefixlen = 1) + " "
	if ip_dir + " spec" in layer:
		ip = layer.split(ip_dir + " spec")[1].strip().split(" ")[0]
		mask = layer.split(ip_dir + " mask")[1].strip().split(" ")[0]
		ip_net = IP(ip + "/" + mask, make_net=True)
		return ip_dir + " net " + ip_net.strCompressed(wantprefixlen = 1) + " "

	return ""

def parse_transport(layer, tp_dir):
	tokens = layer.split(" ")
	if tp_dir + " is" in layer:
		port = layer.split(tp_dir + " is")[1].strip().split(" ")[0]
		return tp_dir + " port " + port + " "
	if tp_dir + " spec" in layer:
		# TODO: Add support for port ranges
		port = int(layer.split(tp_dir + " spec")[1].strip().split(" ")[0])
		mask = int(layer.split(tp_dir + " mask")[1].strip().split(" ")[0])
		return ""

	return ""

def get_flowdir_traffic_class(rule):
	if IPVF in rule:
		return get_flowdir_ipv4_traffic_class(rule)
	elif IPVS in rule:
		return get_flowdir_ipv6_traffic_class(rule)
	else:
		return ""

def get_flowdir_ipv4_traffic_class(rule):
	tc = ""

	layers = rule.split("/")
	for layer in layers:
		layer = layer.strip()

		if IPVF in layer:
			src = parse_ip(layer, SRC)
			tc += src + "&& " if src else ""

			dst = parse_ip(layer, DST)
			tc += dst + "&& " if dst else ""
			continue

		if any(layer.startswith(tp) for tp in [TCP, UDP]):
			proto = layer.split(" ")[0]
			# The protocol must be the first token in this layer
			assert ((proto == TCP) or (proto == UDP)), "Unknown protocol {}".format(proto)
			tc += proto + " "

			src_port = parse_transport(layer, SRC)
			tc += src_port + "&& " if src_port else ""

			dst_port = parse_transport(layer, DST)
			tc += dst_port + "&& " if dst_port else ""
			continue

	# Remove trailing ' && '
	if tc.endswith("&& "):
		tc = tc[:-4]

	return tc.strip()

def get_flowdir_ipv6_traffic_class(rule):
	# TODO: Add support for IPv6
	return ""

def to_json(input_file):
	with open(input_file, "r") as f:
		data = {}
		rule_list = []
		rule_nb = 1
		for line in f:
			line = clean_line(line)

			classifier_type = type_of_classifier(line)
			assert classifier_type != UNKNOWN, "Rule '" + line + "' does not follow the IPFilter/IPLookup/FlowDirector format"

			# Parse the action of this rule
			action = get_action(line, classifier_type)

			# The action defines the port
			if (classifier_type in [IPFILTER, FLOW_DIR] and action == ALLOW):
				port = 0
			elif (classifier_type in [IPFILTER, FLOW_DIR] and action == DENY):
				port = 1
			else:
				port = int(line.split(" ")[1])

			if (classifier_type == IPLOOKUP) and (not line.startswith("dst")):
				# Strip out the port as we already stored this information
				line = line.split(" ")[0]
				if "/32" in line:
					line = "dst host " + line
				else:
					line = "dst net " + line

			tc = line if classifier_type in [IPFILTER, IPLOOKUP] else get_flowdir_traffic_class(line)
			if not tc:
				print("Skipping invalid rule: {}".format(line))
				continue

			# Build a JSON map with the rule's info
			rule_map = {}
			rule_map[TRAFFIC_CLASS_ELEMENT] = tc
			rule_map[OUTPUT_PORT_ELEMENT] = port
			rule_map[PRIORITY_ELEMENT] = rule_nb
			rule_map[ACTION_ELEMENT] = action

			# Add it to the list
			rule_list.append(rule_map)
			rule_nb += 1

		# Build a big JSON wrapper
		data[ENTRIES_ELEMENT] = rule_list

		# Dump to JSON
		json_data = json.dumps(data)

		return json_data

### Execution examples
###     python rules_to_json.py --input-files ~/nfv/metron-testbed/metron-blackbox-metron/load-imbalance/kth/nic_rules/round_robin/100g_offl_round_robin_queues_1 --input-format flowdir
###     python rules_to_json.py --input-folder ~/nfv/metron-testbed/metron-blackbox-metron/load-imbalance/kth/nic_rules/round_robin/ --input-format flowdir

if __name__ == "__main__":
	parser = argparse.ArgumentParser("Click IPFilter/IPClassifier/Lookup to JSON configuration")
	parser.add_argument("--input-format", type=str, default=RULE_FORMAT_CLICK, choices=['click', 'flowdir'], help="Format of input rules. Can be [click, flowdir]")
	parser.add_argument("--input-files", nargs='*', help="Set of space separated input files with IP-level Click rules")
	parser.add_argument("--input-folder", default=None, help="Folder that contains a set of input files with IP-level Click rules")

	args = parser.parse_args()

	input_format = args.input_format
	input_folder = args.input_folder
	input_file_list = args.input_files
	if not input_file_list and not input_folder:
		raise RuntimeError(\
			"Specify a list of comma-separated input files with IPFilter configuration or a folder"\
		)

	# Get all the files of a folder
	if not input_file_list:
		input_file_list = get_files_from_folder(input_folder)

	for in_file in input_file_list:
		out_file = get_substring_until_delimiter(in_file, ".") + ".json"
		dump_string_to_file(to_json(in_file), out_file)
