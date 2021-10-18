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
#include "ip6srv6fecdecode.hh"
#include <click/nameinfo.hh>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/glue.hh>
#define MAX(a, b) ((a > b) ? a : b)
CLICK_DECLS

IP6SRv6FECDecode::IP6SRv6FECDecode()
{
    _use_dst_anno = false;
    memset(&_rlc_info, 0, sizeof(rlc_info_decoder_t));
    rlc_fill_muls(_rlc_info.muls);
    _rlc_info.prng = rlc_reset_coefs();
}

IP6SRv6FECDecode::~IP6SRv6FECDecode()
{
}

int
IP6SRv6FECDecode::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

void
IP6SRv6FECDecode::push(int, Packet *p_in)
{
    fec_framework(p_in);
}

String
IP6SRv6FECDecode::read_handler(Element *e, void *thunk)
{
    return "<error>";
}

void
IP6SRv6FECDecode::add_handlers()
{
    add_read_handler("src", read_handler, 0, Handler::CALM);
    add_write_handler("src", reconfigure_keyword_handler, "1 SRC");
    add_read_handler("dst", read_handler, 1, Handler::CALM);
    add_write_handler("dst", reconfigure_keyword_handler, "2 DST");
}

void
IP6SRv6FECDecode::fec_framework(Packet *p_in)
{
    // Manipulate modified packet because we will remove the TLV
    WritablePacket *p = (WritablePacket *)p_in;
    click_ip6 *ip6 = reinterpret_cast<click_ip6 *>(p->data());
    click_ip6_sr *srv6 = reinterpret_cast<click_ip6_sr *>(p->data() + sizeof(click_ip6));
    int err;

    // Find TLV: source or repair symbol
    uint8_t tlv_type = 0;
    uint16_t start_tlv_offset = 8 + (srv6->last_entry + 1) * 16;
    uint16_t total_tlv_size = srv6->ip6_hdrlen * 8 - start_tlv_offset;
    uint16_t read_bytes = 0;
    uint8_t *tlv_ptr;
    click_chatter("encore: %u %u", srv6->ip6_hdrlen, srv6->last_entry);
    click_chatter("loop: %u %u %u", start_tlv_offset, total_tlv_size, read_bytes);
    while (read_bytes < total_tlv_size) { // Iterate over all TLVs of the SRH
        tlv_ptr = (uint8_t *)srv6 + start_tlv_offset + read_bytes;
        if (tlv_ptr[0] == TLV_TYPE_FEC_SOURCE || tlv_ptr[0] == TLV_TYPE_FEC_REPAIR) {
            tlv_type = tlv_ptr[0];
            break;
        }
        read_bytes += tlv_ptr[1];
    }

    // Not a source or repair symbol
    if (tlv_type != TLV_TYPE_FEC_SOURCE && tlv_type != TLV_TYPE_FEC_REPAIR) {
        output(0).push(p_in);
        return;
    }

    if (tlv_type == TLV_TYPE_FEC_SOURCE) {
        // Load TLV locally
        source_tlv_t source_tlv;
        memset(&source_tlv, 0, sizeof(source_tlv_t));
        memcpy(&source_tlv, tlv_ptr, sizeof(source_tlv_t));

        // Remove the TLV from the source packet
        remove_tlv_source_symbol(p, tlv_ptr - p->data()); // Cleaner way?

        click_chatter("RECEIVED SOURCE SYMBOL");
        // Call FEC Scheme
        // TODO
    } else {
        // Load TLV locally
        repair_tlv_t repair_tlv;
        memset(&repair_tlv, 0, sizeof(repair_tlv_t));
        memcpy(&repair_tlv, tlv_ptr, sizeof(repair_tlv_t));

        click_chatter("RECEIVED REPAIR SYMBOL");

        // Call FEC Scheme
    }

    // Send the (modified, without TLV) source symbol
    // i.e., do not send the repair symbol out of the tunnel
    // TODO: really without the TLV? is it really worth it?
    if (tlv_type == TLV_TYPE_FEC_SOURCE) {
        output(0).push(p);
    }
}

int
IP6SRv6FECDecode::fec_scheme_source(Packet *p_in, source_tlv_t *tlv)
{
    // Store packet as source symbol
    store_source_symbol(p_in, tlv);

    return 0;
}

int
IP6SRv6FECDecode::fec_scheme_repair(Packet *p_in, repair_tlv_t *tlv)
{
    // Store packet as source symbol
    store_repair_symbol(p_in, tlv);

    // TODO: find a more efficient way to trigger FEC recovery
    // For now: simply check if there is at least one lost source
    // symbol in the horizon
    uint32_t encoding_symbol_id = tlv->rfpid;
    uint8_t lost_symbols = 0;
    for (int i = 0; i < DECODER_MAX_WINDOWS * tlv->nss; ++i) {
        srv6_fec2_source_t *symbol = _rlc_info.source_buffer[(encoding_symbol_id - i) % SRV6_FEC_BUFFER_SIZE];
        if (!symbol | symbol->tlv.sfpid != encoding_symbol_id - i) {
            ++lost_symbols;
        }
    }
    if (lost_symbols > 0) {
        click_chatter("GOTTA CALL RECOVERY");
    }

    return 0;
}

void
IP6SRv6FECDecode::store_source_symbol(Packet *p_in, source_tlv_t *tlv) {
    uint32_t encoding_symbol_id = tlv->sfpid;

    // Store the source symbol
    srv6_fec2_source_t *symbol = (srv6_fec2_source_t *)CLICK_LALLOC(sizeof(srv6_fec2_source_t));
    // TODO: check if call failed?
    memset(symbol, 0, sizeof(srv6_fec2_source_t));
    memcpy(&symbol->tlv, tlv, sizeof(source_tlv_t));
    symbol->p = p_in->clone();

    // Clean previous symbol in the buffer and replace with current symbol
    srv6_fec2_source_t *previous_symbol = _rlc_info.source_buffer[encoding_symbol_id % SRV6_FEC_BUFFER_SIZE];
    if (previous_symbol) {
        previous_symbol->p->kill();
        free(previous_symbol);
    }
    _rlc_info.source_buffer[encoding_symbol_id % SRV6_FEC_BUFFER_SIZE] = symbol;
}

void
IP6SRv6FECDecode::store_repair_symbol(Packet *p_in, repair_tlv_t *tlv)
{
    uint32_t encoding_symbol_id = tlv->rfpid;

    // Store the repair symbol
    srv6_fec2_repair_t *symbol = (srv6_fec2_repair_t *)CLICK_LALLOC(sizeof(srv6_fec2_repair_t));
    // TODO: check if call failed?
    memset(symbol, 0, sizeof(srv6_fec2_repair_t));
    memcpy(&symbol->tlv, tlv, sizeof(repair_tlv_t));
    symbol->p = p_in->clone();

    // Clean previous symbol in the buffer and replace with current symbol
    srv6_fec2_repair_t *previous_symbol = _rlc_info.repair_buffer[encoding_symbol_id % SRV6_FEC_BUFFER_SIZE];
    if (previous_symbol) {
        previous_symbol->p->kill();
        free(previous_symbol);
    }
    _rlc_info.repair_buffer[encoding_symbol_id % SRV6_FEC_BUFFER_SIZE] = symbol;
}

void
IP6SRv6FECDecode::remove_tlv_source_symbol(WritablePacket *p, uint16_t offset_tlv)
{
    // Update payload length of IPv6 Header and SRv6 Header
    click_ip6 *ip6 = reinterpret_cast<click_ip6 *>(p->data());
    click_ip6_sr *srv6 = reinterpret_cast<click_ip6_sr *>(p->data() + sizeof(click_ip6));
    ip6->ip6_plen -= htons(sizeof(source_tlv_t));
    srv6->ip6_hdrlen -= 1;

    // Push everything before the TLV, sizeof(tlv) after
    memmove(p->data() + sizeof(source_tlv_t), p->data(), offset_tlv);
    p->pull(sizeof(source_tlv_t));
}

uint8_t
IP6SRv6FECDecode::rlc_get_coef(tinymt32_t *prng)
{
    uint8_t coef = tinymt32_generate_uint32(prng);
    if (coef == 0) return 1;
    return coef;
}

tinymt32_t
IP6SRv6FECDecode::rlc_reset_coefs() {
    tinymt32_t prng;
    prng.mat1 = 0x8f7011ee;
    prng.mat2 = 0xfc78ff1f;
    prng.tmat = 0x3793fdff;
    return prng;
}

void IP6SRv6FECDecode::rlc_fill_muls(uint8_t muls[256 * 256]) {
    for (int i = 0; i < 256; ++i) {
        for (int j = 0; j < 256; ++j) {
            muls[i * 256 + j] = gf256_mul_formula(i, j);
        }
    }
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IP6SRv6FECDecode)
ELEMENT_MT_SAFE(IP6SRv6FECDecode)

