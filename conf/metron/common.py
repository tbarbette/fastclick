#!/usr/bin/python
# -*- coding: utf-8 -*-

from random import randint
from os.path import join
from IPy import IP, _prefixlenToNetmask

ALLOW = "allow"
DROP  = "drop"
DENY  = "deny"

IPCLASSIFIER = "IPClassifier"
IPFILTER     = "IPFilter"
IPLOOKUP     = "IPLookup"
UNKNOWN      = "Unknown"

ENTRIES_ELEMENT = "entries"
TRAFFIC_CLASS_ELEMENT = "trafficClass"
OUTPUT_PORT_ELEMENT = "outputPort"
PRIORITY_ELEMENT = "priority"
ACTION_ELEMENT = "action"

ETHERNET    = "eth"
IPVF        = "ipv4"
IPVS        = "ipv6"
UDP         = "udp"
TCP         = "tcp"
PROTO_EXACT = "proto is"
SRC         = "src"
SRC_EXACT   = "src is"
SRC_SPEC    = "src spec"
SRC_MASK    = "src mask"
DST         = "dst"
DST_EXACT   = "dst is"
DST_SPEC    = "dst spec"
DST_MASK    = "dst mask"

PROTO_UDP = "udp"
PROTO_TCP = "tcp"
PROTO_RANDOM = "random"

STRATEGY_FILE = "file"
STRATEGY_RAND = "random"

CANNOT_SPECIFY_RULES_NB = -1

######################################################################################################

def get_substring_until_delimiter(string, delimiter):
	if string.rfind(delimiter) > 1:
		return string.split(delimiter)[0]
	return string

def strip_last_occurence(string, char_to_strip=","):
	idx = string.rfind(char_to_strip)
	if idx >= 0:
		string = string[:(idx)] + string[(idx+1):]
	return string

def strip_char(line, char_to_strip=","):
	return line.replace(char_to_strip, "").strip()

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

def get_ip_proto_str_from_rule(rule):
	if UDP in rule:
		return UDP
	elif TCP in rule:
		return TCP
	return ""

def get_pattern_value(rule, pattern, verbose=False):
	assert pattern, "Cannot extract value from non-existent pattern"
	rest = strip_char(rule.split(pattern)[1], ",")
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
			line = strip_char(line, ",")

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

			proto = get_ip_proto_str_from_rule(line)
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

def get_ip_proto_str(proto):
	proto_str = ""
	if proto == PROTO_RANDOM:
		proto_str = UDP if randint(0, 1) == 0 else TCP
	else:
		proto_str = UDP if proto == PROTO_UDP else TCP
	return proto_str

def ip_proto_str_to_int(proto_str):
	if proto_str == TCP:
		return 6
	elif proto_str == UDP:
		return 17
	return -1

def get_random_port():
	return randint(0, 65535)

def get_random_rule(rule_nb, proto):
	proto_str = get_ip_proto_str(proto)

	rule_map = {}
	rule_map[IPVF] = {}
	if proto_str:
		rule_map[IPVF][PROTO_EXACT] = ip_proto_str_to_int(proto_str)
	rule_map[IPVF][SRC_EXACT] = get_random_ipv4_address()
	rule_map[IPVF][DST_EXACT] = get_random_ipv4_address()
	if proto_str:
		rule_map[proto_str] = {}
		rule_map[proto_str][SRC_EXACT] = str(get_random_port())
		rule_map[proto_str][DST_EXACT] = str(get_random_port())
	rule_map[OUTPUT_PORT_ELEMENT] = 0
	rule_map[PRIORITY_ELEMENT] = rule_nb
	rule_map[ACTION_ELEMENT] = ALLOW

	return rule_map

######################################################################################################
