/*
 * ip6encap.{cc,hh} -- element encapsulates packet in IP6 header
 * Roman Chertov
 *
 * Copyright (c) 2021 IP Networking Lab, UCLouvain
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include "ip6srv6fec.hh"
#include <click/nameinfo.hh>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/glue.hh>
#define MAX(a, b) ((a > b) ? a : b)
CLICK_DECLS

IP6SRv6FECEncode::IP6SRv6FECEncode()
{
    _use_dst_anno = false;
    memset(&_rlc_info, 0, sizeof(rlc_info_t));
    _rlc_info.window_size = 4; // TODO: dynamically change
    _rlc_info.window_step = 2; // TODO: dynamically change
    _repair_packet = Packet::make(LOCAL_MTU);
    rlc_init_muls();
    rlc_reset_coefs();
}

IP6SRv6FECEncode::~IP6SRv6FECEncode()
{
}

int
IP6SRv6FECEncode::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
	.read_mp("ENC", enc)
	.read_mp("DEC", dec)
	.complete() < 0)
        return -1;

    return 0;
}

void
IP6SRv6FECEncode::push(int, Packet *p_in)
{
    fec_framework(p_in);
}

String
IP6SRv6FECEncode::read_handler(Element *e, void *thunk)
{
    return "<error>";
}

void
IP6SRv6FECEncode::add_handlers()
{
    add_read_handler("src", read_handler, 0, Handler::CALM);
    add_write_handler("src", reconfigure_keyword_handler, "1 SRC");
    add_read_handler("dst", read_handler, 1, Handler::CALM);
    add_write_handler("dst", reconfigure_keyword_handler, "2 DST");
}

void
IP6SRv6FECEncode::fec_framework(Packet *p_in)
{
    const click_ip6 *ip6 = reinterpret_cast<const click_ip6 *>(p_in->data());
    const click_ip6_sr *srv6 = reinterpret_cast<const click_ip6_sr *>(p_in->data() + sizeof(click_ip6));
    int err;

    // Call FEC Scheme
    err = IP6SRv6FECEncode::fec_scheme(p_in);
    if (err < 0) { return; }

    // TODO: currently make two copies to be sure not to override values
    //       how could we improve the performance
    uint16_t srv6_len = 8 + srv6->ip6_hdrlen * 8;
    uint8_t buffer1[40];
    uint8_t buffer2[srv6_len];
    // Copy the IPv6 Header and SRv6 Header
    memcpy(buffer1, ip6, 40);
    memcpy(buffer2, srv6, srv6_len);

    // Extend the packet to add the TLV
    WritablePacket *p = p_in->push(sizeof(source_tlv_t));
    if (!p)
        return;
    // Add the TLV
    memcpy(p->data(), buffer1, 40);
    memcpy(p->data() + 40, buffer2, srv6_len);
    _source_tlv.type = TLV_TYPE_FEC_SOURCE;
    _source_tlv.len = sizeof(source_tlv_t) - 2; // Ignore type and len
    memcpy(p->data() + 40 + srv6_len, &_source_tlv, sizeof(source_tlv_t));

    // Update the new length of the SRv6 Header
    click_ip6_sr *srv6_update = reinterpret_cast<click_ip6_sr *>(p->data() + 40);
    ++srv6_update->ip6_hdrlen;
    click_ip6 *ip6_update = reinterpret_cast<click_ip6 *>(p->data());
    ip6_update->ip6_ctlun.ip6_un1.ip6_un1_plen += htons(8);
    output(0).push(p);

    if (err == 1) { // Repair
        // Encapsulate repair symbol in packet
        // IPv6 and SRv6 Headers
        click_ip6 *r_ip6 = reinterpret_cast<click_ip6 *>(_repair_packet->data());
        click_ip6_sr *r_srv6 = reinterpret_cast<click_ip6_sr *>(_repair_packet->data() + sizeof(click_ip6));
        repair_tlv_t *r_tlv = reinterpret_cast<repair_tlv_t *>(_repair_packet->data() + sizeof(click_ip6) + 8 + 32);

        // IPv6 Header
        memcpy(&r_ip6->ip6_src, enc.data(), sizeof(IP6Address));
        memcpy(&r_ip6->ip6_dst, dec.data(), sizeof(IP6Address));
        r_ip6->ip6_flow = htonl(6 << IP6_V_SHIFT);
        r_ip6->ip6_plen = htons(_rlc_info.max_length + sizeof(click_ip6) + sizeof(click_ip6_sr) + sizeof(repair_tlv_t) + 2 * sizeof(IP6Address));
        r_ip6->ip6_nxt = IPPROTO_ROUTING;
        r_ip6->ip6_hlim = 53;

        r_srv6->type = IP6PROTO_SEGMENT_ROUTING;
        r_srv6->segment_left = 1;
        r_srv6->last_entry = 1;
        r_srv6->flags = 0;
        r_srv6->tag = 0;
        r_srv6->ip6_sr_next = 0;
        r_srv6->ip6_hdrlen = sizeof(repair_tlv_t) + 2 * sizeof(IP6Address);
        memcpy(&r_srv6->segments[0], enc.data(), sizeof(IP6Address));
        memcpy(&r_srv6->segments[1], dec.data(), sizeof(IP6Address));
        // Add repair TLV
        memcpy(r_tlv, &_repair_tlv, sizeof(repair_tlv_t));

        // Send repair packet
        output(0).push(_repair_packet);

        // Reset parameters of the RLC information
        _rlc_info.max_length = 0;
        rlc_reset_coefs();
        _repair_packet = Packet::make(LOCAL_MTU);
        if (!_repair_packet) {
            return; // What to do ?
        }
    }
}

int
IP6SRv6FECEncode::fec_scheme(Packet *p_in)
{
    // Complete the source TLV
    _source_tlv.type = TLV_TYPE_FEC_SOURCE;
    _source_tlv.len = sizeof(source_tlv_t) - 2; // Ignore type and len
    _source_tlv.padding = 0;
    _source_tlv.sfpid = _rlc_info.encoding_symbol_id;

    // Store packet aka on-the-line coding
    rlc_encode_otl(p_in);

    // Update RLC information
    ++_rlc_info.buffer_size;
    ++_rlc_info.encoding_symbol_id;

    // Generate a repair symbol if full window
    if (_rlc_info.buffer_size == _rlc_info.window_size) {
        // Complete the Repair FEC Payload ID
        _repair_tlv.type = TLV_TYPE_FEC_REPAIR;
        _repair_tlv.len = sizeof(repair_tlv_t) - 2;
        _repair_tlv.padding = 0;
        _repair_tlv.rfpid = _source_tlv.sfpid;
        _repair_tlv.rfi = (15 << (24) + (_rlc_info.window_step << 16)) + _rlc_info.repair_key;
        _repair_tlv.nss = _rlc_info.window_size;
        _repair_tlv.nrs = 1;

        // Update RLC informations after repair
        _rlc_info.buffer_size -= _rlc_info.window_step;
        ++_rlc_info.repair_key;
        
        return 1;
    }

    return 0;
}

void
IP6SRv6FECEncode::rlc_encode_otl(Packet *p)
{
    // Leave room for the IPv6 Header, SRv6 Header (3 segments) and repair TLV
    uint8_t repair_offset = 40 + 8 + 16 * 3 + sizeof(repair_tlv_t);

    // TODO: cancel varying fields ?

    // Get coefficient for this source symbol
    uint8_t coef = rlc_get_coef();

    // Encode the packet in the repair symbol
    symbol_add_scaled(_repair_packet->data() + repair_offset, coef, p->data(), p->length(), _rlc_info.muls);

    // Encode the packet length
    uint16_t packet_length = p->length(); // Cast in uint16_t because 16 bits for IPv6 packet length
    symbol_add_scaled(&_repair_tlv.coded_length, coef, &packet_length, sizeof(uint16_t), _rlc_info.muls);

    // Update maximum length = repair payload length
    _rlc_info.max_length = MAX(_rlc_info.max_length, packet_length);
}

// TODO: retirer
uint8_t
IP6SRv6FECEncode::rlc_get_coef()
{
    uint8_t coef = tinymt32_generate_uint32(&_rlc_info.prng);
    if (coef == 0) return 1;
    return coef;
}

// TODO: retirer
void
IP6SRv6FECEncode::rlc_reset_coefs()
{
    tinymt32_t new_prng;
    new_prng.mat1 = 0x8f7011ee;
    new_prng.mat2 = 0xfc78ff1f;
    new_prng.tmat = 0x3793fdff;
    _rlc_info.prng = new_prng;
}

// TODO: retirer
void IP6SRv6FECEncode::rlc_init_muls()
{
    for (int i = 0; i < 256; ++i) {
        for (int j = 0; j < 256; ++j) {
            _rlc_info.muls[i * 256 + j] = gf256_mul_formula(i, j);
        }
    }
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IP6SRv6FECEncode)
ELEMENT_MT_SAFE(IP6SRv6FECEncode)
