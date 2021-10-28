/*
 * ip6encap.{cc,hh} -- element encapsulates packet in IP6 header
 * Louis Navarre
 * Tom Barbette
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
    rlc_fill_muls(_rlc_info.muls);
    _rlc_info.prng = rlc_reset_coefs();
}

IP6SRv6FECEncode::~IP6SRv6FECEncode()
{
    static_assert(sizeof(source_tlv_t) == 8, "source_tlv_t should be 8 bytes");

    for (int i = 0; i < SRV6_FEC_BUFFER_SIZE; ++i) {
        Packet *packet = _rlc_info.source_buffer[i];
        if (packet) {
            packet->kill();
        }  
    }
}

int
IP6SRv6FECEncode::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
	.read_mp("ENC", enc)
	.read_mp("DEC", dec)
    .read_mp("FED", fed)
    .read_or_set("WINDOW", _rlc_info.window_size, 4)
    .read_or_set("STEP", _rlc_info.window_step, 2)
    .read_or_set("SCHEME", _fec_scheme, SRV6_FEC_RLC)
	.complete() < 0)
        return -1;

    return 0;
}

void
IP6SRv6FECEncode::push(int, Packet *p_in)
{
    fec_framework(p_in, [this](Packet*p){output(0).push(p);});
}

#if HAVE_BATCH
void 
IP6SRv6FECEncode::push_batch(int, PacketBatch *batch) {
    EXECUTE_FOR_EACH_PACKET_ADD( fec_framework, batch );
    if (batch)
        output_push_batch(0, batch);

}
#endif

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

inline void
IP6SRv6FECEncode::fec_framework(Packet *p_in, std::function<void(Packet*)>push)
{
    int err;

    // Check the SID to determine if it is a source symbol or a feedback packet
    const click_ip6 *ip6 = reinterpret_cast<const click_ip6 *>(p_in->data());
    uint32_t *ip_32 = (uint32_t *)&ip6->ip6_dst.s6_addr;
    uint32_t *fb_32 = (uint32_t *)fed.data32();
    // We know that the packet belongs either to the FEC Framework or the Feedback process
    // The difference between the two SIDs must be located in the last 32 bits of the IPv6 Address
    // to make the following work
    if (ip_32[3] = fb_32[3]) {
        // Feedback message
        
        
        // Do not forward a feedback message
        return;
    }
    // This is a source symbol

    // Call FEC Scheme
    err = IP6SRv6FECEncode::fec_scheme(p_in);
    if (err < 0) { 
        return; 
    }
    
    WritablePacket *p = srv6_fec_add_source_tlv(p_in, &_source_tlv);
    if (!p) {
        if (err == 1) {
            _repair_packet->kill();
        }
        return; //Memory problem, packet is already destroyed
    }
    push(p);

    if (err == 1) { // Repair
        if (!_repair_packet) {
            click_chatter("No repair packet TODO");
            return;
        }
        encapsulate_repair_payload(_repair_packet, &_repair_tlv, &enc, &dec, _rlc_info.max_length);

        // Send repair packet
//        click_chatter("Send repair symbol");
        push(_repair_packet);
        _repair_packet = 0;

        // Reset parameters of the RLC information
        _rlc_info.max_length = 0;
        memset(&_repair_tlv, 0, sizeof(repair_tlv_t));
        _rlc_info.prng = rlc_reset_coefs();

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

    // Store packet as source symbol
    store_source_symbol(p_in, _rlc_info.encoding_symbol_id);

    // Store the maximum length = length of the repair packet
    _rlc_info.max_length = MAX(_rlc_info.max_length, p_in->length());

    // Update RLC information
    ++_rlc_info.buffer_size;
    ++_rlc_info.encoding_symbol_id;

    // Generate a repair symbol if full window
    if (_rlc_info.buffer_size == _rlc_info.window_size) {
        // Create new repair packet with correct size
        _repair_packet = Packet::make(_rlc_info.max_length + sizeof(click_ip6) + sizeof(click_ip6_sr) + sizeof(repair_tlv_t) + 2 * sizeof(IP6Address));
        if (!_repair_packet) {
            click_chatter("Cannot get repair packet TODO");
            return -1;
        }

        memset(_repair_packet->data(), 0, _repair_packet->length());

        // Encode the source symbols in repair
        if (_fec_scheme == SRV6_FEC_RLC) {
            rlc_encode_symbols(_source_tlv.sfpid);
        } else {
            xor_encode_symbols(_source_tlv.sfpid);
        }

        // Free the out-of-window source symbols
        // Maybe this will give a problem once we support adaptative RLC
        // Because we might free data that should be used after if the window
        // size increases
        free_out_of_window(_source_tlv.sfpid);

        // Complete the Repair FEC Payload ID
        _repair_tlv.type = TLV_TYPE_FEC_REPAIR;
        _repair_tlv.len = sizeof(repair_tlv_t) - 2;
        _repair_tlv.padding = _fec_scheme;
        _repair_tlv.rfpid = _source_tlv.sfpid;
        _repair_tlv.rfi = ((_rlc_info.window_step << 24) + (_rlc_info.window_step << 16)) + _rlc_info.repair_key;
        _repair_tlv.nss = _rlc_info.window_size;
        _repair_tlv.nrs = 1;

        // Update RLC informations after repair
        _rlc_info.buffer_size -= _rlc_info.window_step;
        ++_rlc_info.repair_key;

        _rlc_info.previous_window_step = _rlc_info.window_step;
        // Update coding rate
        // TODO
        
        return 1;
    }

    return 0;
}

void
IP6SRv6FECEncode::store_source_symbol(Packet *p_in, uint32_t encoding_symbol_id)
{
    _rlc_info.source_buffer[encoding_symbol_id % SRV6_FEC_BUFFER_SIZE] = p_in->clone();
}

void
IP6SRv6FECEncode::rlc_encode_symbols(uint32_t encoding_symbol_id)
{
    tinymt32_t prng = _rlc_info.prng;
    // tinymt32_init(&prng, _rlc_info.repair_key);
    tinymt32_init(&prng, 1);
    // encoding_symbol_id: of the last source symbol (i.e. of the repair symbol)
    uint32_t start_esid = encoding_symbol_id - _rlc_info.window_size + 1;
    for (int i = 0; i < _rlc_info.window_size; ++i) {
        uint8_t idx = (start_esid + i) % SRV6_FEC_BUFFER_SIZE;
        Packet *source_symbol = _rlc_info.source_buffer[idx];

        rlc_encode_one_symbol(source_symbol, _repair_packet, &prng, _rlc_info.muls, &_repair_tlv);
    }
}

void
IP6SRv6FECEncode::xor_encode_symbols(uint32_t encoding_symbol_id)
{
    uint32_t start_esid = encoding_symbol_id - _rlc_info.window_size + 1;
    for (int i = 0; i < _rlc_info.window_size; ++i) {
        uint8_t idx = (start_esid + i) % SRV6_FEC_BUFFER_SIZE;
        Packet *source_symbol = _rlc_info.source_buffer[idx];

        xor_encode_one_symbol(source_symbol, _repair_packet, &_repair_tlv);
    }
}

void
IP6SRv6FECEncode::xor_encode_one_symbol(Packet *s, WritablePacket *r, repair_tlv_t *repair_tlv)
{
    // Leave room for the IPv6 Header, SRv6 Header (3 segments) and repair TLV
    uint8_t repair_offset = 40 + 8 + 16 * 2 + sizeof(repair_tlv_t);
    uint8_t *s_64 = (uint8_t *)s->data();
    uint8_t *r_64 = (uint8_t *)(r->data() + repair_offset);

    for (uint16_t i = 0; i < s->length() / sizeof(uint8_t); ++i) {
        r_64[i] ^= s_64[i];
    }

    // Also code the potential remaining data
    uint8_t *s_8 = (uint8_t *)s->data();
    uint8_t *r_8 = (uint8_t *)(r->data() + repair_offset);
    for (uint16_t i = (s->length() / sizeof(uint8_t)) * sizeof(uint8_t); i < s->length(); ++i) {
        r_8[i] ^= s_8[i];
    }

    // Encode the packet length
    uint16_t coded_length = repair_tlv->coded_length;
    repair_tlv->coded_length ^= s->length();
}

void
IP6SRv6FECEncode::free_out_of_window(uint32_t encoding_symbol_id) {
    for (int i = 0; i < _rlc_info.window_step; ++i) {
        uint16_t buffer_idx = encoding_symbol_id - _rlc_info.window_size + i + 1;
        Packet *p_kill = _rlc_info.source_buffer[buffer_idx % SRV6_FEC_BUFFER_SIZE];
        if (!p_kill) {
            click_chatter("SRv6-FEC: empty packet to kill");
            continue;
        }
        
        // Clean entry in the buffer
        _rlc_info.source_buffer[buffer_idx % SRV6_FEC_BUFFER_SIZE] = 0;

        // Kill packet and free memory
        p_kill->kill();
    }
}

tinymt32_t
IP6SRv6FECEncode::rlc_reset_coefs() {
    tinymt32_t prng;
    prng.mat1 = 0x8f7011ee;
    prng.mat2 = 0xfc78ff1f;
    prng.tmat = 0x3793fdff;
    return prng;
}

void IP6SRv6FECEncode::rlc_fill_muls(uint8_t muls[256 * 256]) {
    for (int i = 0; i < 256; ++i) {
        for (int j = 0; j < 256; ++j) {
            muls[i * 256 + j] = gf256_mul_formula(i, j);
        }
    }
}

uint8_t IP6SRv6FECEncode::rlc_get_coef(tinymt32_t *prng) {
    uint8_t coef = tinymt32_generate_uint32(prng);
    if (coef == 0) return 1;
    return coef;
}

void IP6SRv6FECEncode::rlc_encode_one_symbol(Packet *s, WritablePacket *r, tinymt32_t *prng, uint8_t muls[256 * 256 * sizeof(uint8_t)], repair_tlv_t *repair_tlv) {
    // Leave room for the IPv6 Header, SRv6 Header (3 segments) and repair TLV
    uint8_t repair_offset = 40 + 8 + 16 * 2 + sizeof(repair_tlv_t);

    // TODO: cancel varying fields?

    // Get coefficient for this source symbol
    uint8_t coef = rlc_get_coef(prng);

    uint16_t packet_length = s->length(); // Cast in uint16_t because 16 bits for IPv6 packet length

    // Encode the packet in the repair symbol
    symbol_add_scaled(r->data() + repair_offset, coef, s->data(), packet_length, muls);

    // Encode the packet length
    uint16_t coded_length = repair_tlv->coded_length;
    symbol_add_scaled(&coded_length, coef, &packet_length, sizeof(uint16_t), muls);
    repair_tlv->coded_length = coded_length;
}

WritablePacket * IP6SRv6FECEncode::srv6_fec_add_source_tlv(Packet *p_in, source_tlv_t *tlv) {
    const click_ip6 *ip6 = reinterpret_cast<const click_ip6 *>(p_in->data());
    const click_ip6_sr *srv6 = (const click_ip6_sr *)ip6_find_header(ip6, IP6_EH_ROUTING, p_in->end_data());
    if (!srv6) {
        p_in->kill();
        click_chatter("Not an SRv6 packet!");
        return 0;
    }
    
    unsigned srv6_offset = (unsigned char*)srv6 - (unsigned char*)ip6;

    // Extend the packet to add the TLV
    WritablePacket *p = p_in->push(sizeof(source_tlv_t));
    if (!p)
        return 0;

    uint16_t srv6_len = 8 + srv6->ip6_hdrlen * 8;

    // Move headers and add TLV
    memmove(p->data(), p->data() + sizeof(source_tlv_t), srv6_len + srv6_offset);
    memcpy(p->data() + sizeof(click_ip6) + srv6_len, tlv, sizeof(source_tlv_t));

    // Update the new length of the SRv6 Header
    click_ip6_sr *srv6_update = reinterpret_cast<click_ip6_sr *>(p->data() + srv6_offset);
    srv6_update->ip6_hdrlen += sizeof(source_tlv_t)/8;
    click_ip6 *ip6_update = reinterpret_cast<click_ip6 *>(p->data());
    ip6_update->ip6_plen = htons(ntohs(ip6_update->ip6_plen) + sizeof(source_tlv_t));
    p->set_network_header(p->data(), srv6_offset + srv6_len + sizeof(source_tlv_t));
    return p;
}

void IP6SRv6FECEncode::encapsulate_repair_payload(WritablePacket *p, repair_tlv_t *tlv, IP6Address *encoder, IP6Address *decoder, uint16_t packet_length) {
    // IPv6 and SRv6 Header pointers
    click_ip6 *r_ip6 = reinterpret_cast<click_ip6 *>(p->data());
    click_ip6_sr *r_srv6 = reinterpret_cast<click_ip6_sr *>(p->data() + sizeof(click_ip6));
    repair_tlv_t *r_tlv = reinterpret_cast<repair_tlv_t *>(p->data() + sizeof(click_ip6) + 8 + 32);

    // IPv6 Header
    memcpy(&r_ip6->ip6_src, encoder->data(), sizeof(IP6Address));
    memcpy(&r_ip6->ip6_dst, decoder->data(), sizeof(IP6Address));
    r_ip6->ip6_flow = htonl(6 << IP6_V_SHIFT);
    r_ip6->ip6_plen = htons(packet_length + sizeof(click_ip6_sr) + sizeof(repair_tlv_t) + 2 * sizeof(IP6Address));
    r_ip6->ip6_nxt = IPPROTO_ROUTING;
    r_ip6->ip6_hlim = 53;

    // SRv6 Header
    r_srv6->type = IP6PROTO_SEGMENT_ROUTING;
    r_srv6->segment_left = 1;
    r_srv6->last_entry = 1;
    r_srv6->flags = 0;
    r_srv6->tag = 0;
    r_srv6->ip6_sr_next = 253;
    r_srv6->ip6_hdrlen = (sizeof(repair_tlv_t) + 2 * sizeof(IP6Address)) / 8;
    memcpy(&r_srv6->segments[0], encoder->data(), sizeof(IP6Address));
    memcpy(&r_srv6->segments[1], decoder->data(), sizeof(IP6Address));
    
    // Add repair TLV
    memcpy(r_tlv, tlv, sizeof(repair_tlv_t));

    // Set annotations
    _repair_packet->set_network_header(p->data(), (unsigned char*)(r_tlv + 1) - p->data());
}

void
IP6SRv6FECEncode::feedback_message(Packet *p_in)
{
    const click_ip6 *ip6 = reinterpret_cast<const click_ip6 *>(p_in->data());
    const click_ip6_sr *srv6 = reinterpret_cast<const click_ip6_sr *>(p_in->data() + sizeof(click_ip6));
    const feedback_tlv_t *tlv = reinterpret_cast<const feedback_tlv_t *>(p_in->data() + sizeof(click_ip6) + sizeof(click_ip6_sr) + (srv6->last_entry + 1) * 16);

    // Hard thresholds defining the algorithm
    // TODO: put somewhere else
    double SRV6_FEC_FB_LOST_THRESH = 0.05;
    double SRV6_FEC_FB_BURST_MIN = 2.0;
    double SRV6_FEC_FB_ALPHA = 0.5;
    
    // No feedback, i.e. no protected traffic since last update
    // Reset the values by default
    if (tlv->nb_theoric == 0) {
        _rlc_info.window_size = 8; // TODO: make auto
        _rlc_info.window_step = 2; // TODO: make auto
        click_chatter("Reset the values of the FEC Scheme algorithm");
        return;
    }
    
    // No lost packet in the window
    // Increase the window step to generate less repair symbols
    if (tlv->nb_lost == 0) {
        _rlc_info.loss_estimation = 0;
        _rlc_info.generate_repair_symbols = false;
        return;
    }

    double lost = _rlc_info.loss_estimation;
    // Update the lost estimation gradually
    lost = (tlv->nb_lost / tlv->nb_theoric) * SRV6_FEC_FB_ALPHA + (lost) * (1.0 - SRV6_FEC_FB_ALPHA);

    // Compute the redundancy ratio compared to the data packets
    double redundancy = 1.0 / _rlc_info.window_step;

    // There are more loss than redundancy packets
    // Reduce the window step to increase redundancy
    if (redundancy < lost && _rlc_info.window_step > 1) {
        --_rlc_info.window_step;
        // TODO: Also generate immediate repair symbols ?
    }

    // Update the window size by inspecting the possible burst losses in the data
    uint8_t max_seen_burst = 0;
    uint8_t current_burst = 0;
    uint64_t lost_bitstring = (~tlv->bit_string) & ((1 << tlv->nb_theoric) - 1);
    while (lost_bitstring > 0) {
        if (lost_bitstring & 1) {
            ++current_burst;
        } else {
            max_seen_burst = MAX(max_seen_burst, current_burst);
            current_burst = 0;
        }
        lost_bitstring >>=2;
    }
    // Compute the maximum burst length we can recover with the current 
    uint8_t theoric_max_burst = ceil(_rlc_info.window_size / (double)_rlc_info.window_step);
    
    if (theoric_max_burst < max_seen_burst) {
        ++_rlc_info.window_size;
    }
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IP6SRv6FECEncode)
ELEMENT_MT_SAFE(IP6SRv6FECEncode)
