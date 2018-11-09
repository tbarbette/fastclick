#!/usr/bin/python
# -*- coding: utf-8 -*-

FD_CREATE = "flow create"

ALLOW = "allow"
DROP  = "drop"
DENY  = "deny"

INGRESS = "ingress"
EGRESS = "egress"
TRANSFER = "transfer"

IPCLASSIFIER = "IPClassifier"
IPFILTER     = "IPFilter"
IPLOOKUP     = "IPLookup"
FLOW_DIR     = "FlowDirector"
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
ACTION_Q    = "queue index"
ACTION_CNT  = "count"

ETHERNET_RULE_PREF = "ingress pattern eth"
