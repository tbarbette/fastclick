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
#define MIN(a, b) ((a < b) ? a : b)
CLICK_DECLS

IP6SRv6FECDecode::IP6SRv6FECDecode()
{
    _use_dst_anno = false;
    memset(&_rlc_info, 0, sizeof(rlc_info_decoder_t));
    assign_inv(_rlc_info.table_inv);
    rlc_fill_muls(_rlc_info.muls);
}

IP6SRv6FECDecode::~IP6SRv6FECDecode()
{
}

int
IP6SRv6FECDecode::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
	.read_mp("DEC", dec)
	.complete() < 0)
        return -1;
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
    click_chatter("\n\n----- NEW PACKET -----");
    for (int i = 0; i < SRV6_FEC_BUFFER_SIZE; ++i) {
        srv6_fec2_source_t *source = _rlc_info.source_buffer[i];
        if (source) {
            click_chatter("Source before1 #%u (%u): %x %x %x", i, source->p->length(), source->p->data()[0], source->p->data()[1], source->p->data()[2]);
        }
    }
    click_chatter("Au cas ou: voici le paquet recu (%u): %x %x %x", p_in->length(), p_in->data()[0], p_in->data()[1], p_in->data()[2]);
    // Manipulate modified packet because we will remove the TLV
    WritablePacket *p = p_in->uniqueify();
    click_ip6 *ip6 = reinterpret_cast<click_ip6 *>(p->data());
    click_ip6_sr *srv6 = reinterpret_cast<click_ip6_sr *>(p->data() + sizeof(click_ip6));
    int err;

    // Find TLV: source or repair symbol
    uint8_t tlv_type = 0;
    // last_entry is 0-indexed => +1
    uint16_t start_tlv_offset = 8 + (srv6->last_entry + 1) * 16;
    // ip6_hdrlen does not include the 8 first bytes => + 1
    uint16_t total_tlv_size = (srv6->ip6_hdrlen + 1) * 8 - start_tlv_offset;
    uint16_t read_bytes = 0;
    uint8_t *tlv_ptr;
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
        output(0).push(p);
        return;
    }

    if (tlv_type == TLV_TYPE_FEC_SOURCE) {
        // Load TLV locally
        source_tlv_t source_tlv;
        //memset(&source_tlv, 0, sizeof(source_tlv_t));
        memcpy(&source_tlv, tlv_ptr, sizeof(source_tlv_t));

        // Remove the TLV from the source packet
        // remove_tlv_source_symbol(p, tlv_ptr - p->data()); // Cleaner way?

        // Call FEC Scheme
        fec_scheme_source(p, &source_tlv);
    } else {
        /*// Load TLV locally
        repair_tlv_t repair_tlv;
        memset(&repair_tlv, 0, sizeof(repair_tlv_t));
        memcpy(&repair_tlv, tlv_ptr, sizeof(repair_tlv_t));

        // Call FEC Scheme
        fec_scheme_repair(p, &repair_tlv);*/
    }

    // Send the (modified, without TLV) source symbol
    // i.e., do not send the repair symbol out of the tunnel
    if (tlv_type == TLV_TYPE_FEC_SOURCE) {
        output(0).push(p);
    }
    //click_chatter("Test final call");
    for (int i = 0; i < SRV6_FEC_BUFFER_SIZE; ++i) {
        srv6_fec2_source_t *source = _rlc_info.source_buffer[i];
        if (source) {
            click_chatter("Source End #%u (%u): %x %x %x", i, source->p->length(), source->p->data()[0], source->p->data()[1], source->p->data()[2]);
        }
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
    for (int i = 0; i < SRV6_FEC_BUFFER_SIZE; ++i) {
        srv6_fec2_source_t *source = _rlc_info.source_buffer[i];
        if (source) {
            click_chatter("Source before1 #%u: %x %x %x", i, source->p->data()[0], source->p->data()[1], source->p->data()[2]);
        }
    }

    // Store packet as source symbol
    store_repair_symbol(p_in, tlv);

    for (int i = 0; i < SRV6_FEC_BUFFER_SIZE; ++i) {
        srv6_fec2_source_t *source = _rlc_info.source_buffer[i];
        if (source) {
            click_chatter("Source before2 #%u: %x %x %x", i, source->p->data()[0], source->p->data()[1], source->p->data()[2]);
        }
    }

    // Call RLC recovery
    rlc_recover_symbols();

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

    //click_chatter("First few bytes of it stored: %x %x %x", symbol->p->data()[0], symbol->p->data()[1], symbol->p->data()[2]);

    // Clean previous symbol in the buffer and replace with current symbol
    srv6_fec2_source_t *previous_symbol = _rlc_info.source_buffer[encoding_symbol_id % SRV6_FEC_BUFFER_SIZE];
    if (previous_symbol) {
        click_chatter("Deleting previous symbol");
        previous_symbol->p->kill();
        CLICK_LFREE(previous_symbol, sizeof(srv6_fec2_source_t));
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
    symbol->p = p_in->uniqueify();
    // Clean previous symbol in the buffer and replace with current symbol
    srv6_fec2_repair_t *previous_symbol = _rlc_info.repair_buffer[encoding_symbol_id % SRV6_FEC_BUFFER_SIZE];
    if (previous_symbol) {
        previous_symbol->p->kill();
        CLICK_LFREE(previous_symbol, sizeof(srv6_fec2_repair_t));
    }
    _rlc_info.repair_buffer[encoding_symbol_id % SRV6_FEC_BUFFER_SIZE] = symbol;
    _rlc_info.encoding_symbol_id = encoding_symbol_id;
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

void
IP6SRv6FECDecode::rlc_get_coefs(tinymt32_t *prng, uint32_t seed, int n, uint8_t *coefs)
{
    tinymt32_init(prng, seed);
    int i;
    for (i = 0 ; i < n ; i++) {
        coefs[i] = (uint8_t) tinymt32_generate_uint32(prng);
        if (coefs[i] == 0)
            coefs[i] = 1;
    }
}

void
IP6SRv6FECDecode::rlc_fill_muls(uint8_t muls[256 * 256])
{
    for (int i = 0; i < 256; ++i) {
        for (int j = 0; j < 256; ++j) {
            muls[i * 256 + j] = gf256_mul_formula(i, j);
        }
    }
}

void
IP6SRv6FECDecode::rlc_recover_symbols()
{
    uint16_t max_packet_length = 0;
    uint8_t nb_source_symbols = 0;
    uint8_t window_size = 0;
    uint8_t window_step = 0;
    uint8_t previous_window_step = 0;
    uint32_t encoding_symbol_id = _rlc_info.encoding_symbol_id;

    // Init the pseudo random number generator
    tinymt32_t prng;
    memset(&prng, 0, sizeof(tinymt32_t)),
    prng.mat1 = 0x8f7011ee;
    prng.mat2 = 0xfc78ff1f;
    prng.tmat = 0x3793fdff;

    // 1. Detect the size of the system:
    //      - Number of rows (i.e., equations or repair symbols)
    //      - Number of columns (i.e., unknowns or lost source symbols)
    uint8_t nb_windows = 0; // One window = one repair symbol
    uint32_t running_esid = encoding_symbol_id; // esid = encoding symbol id
    for (int i = 0; i < RLC_MAX_WINDOWS; ++i) {
        srv6_fec2_repair_t *repair = _rlc_info.repair_buffer[running_esid % SRV6_FEC_BUFFER_SIZE];
        // Did not receive this repair symbol => stop iteration
        if (!repair || repair->tlv.rfpid != running_esid) {
            break;
        }
        window_size = repair->tlv.nss;
        window_step = (repair->tlv.rfi >> 16) & 0xff;
        previous_window_step = (repair->tlv.rfi >> 24);
        ++nb_windows;
        nb_source_symbols += previous_window_step;
        running_esid -= previous_window_step;
        // No coding before this step
        if (previous_window_step == 0) {
            break;
        }
    }

    // Still need to count the symbols at the beginning of the first window
    nb_source_symbols += window_size - previous_window_step; // TODO: verify this

    // No valid window: no repair symbol received or no FEC applied
    // Should not happen since this function is triggered by the
    // reception of a repair symbol
    if (nb_windows == 0) {
        return;
    }

    // Received source symbols array
    srv6_fec2_source_term_t **ss_array = (srv6_fec2_source_term_t **)CLICK_LALLOC(sizeof(srv6_fec2_source_t *) * nb_source_symbols);
    if (!ss_array) {
        return;
    }
    memset(ss_array, 0, sizeof(srv6_fec2_source_term_t *) * nb_source_symbols);

    // Received repair symbols array
    srv6_fec2_repair_t **rs_array = (srv6_fec2_repair_t **)CLICK_LALLOC(sizeof(srv6_fec2_term_t *) * nb_windows);
    if (!rs_array) {
        CLICK_LFREE(ss_array, sizeof(srv6_fec2_source_term_t *) * nb_source_symbols);
        return;
    }
    memset(rs_array, 0, sizeof(srv6_fec2_repair_t *) * nb_windows);

    uint8_t nb_unknwons = 0;
    uint8_t *x_to_source = (uint8_t *)CLICK_LALLOC(nb_source_symbols * sizeof(uint8_t));
    uint8_t *source_to_x = (uint8_t *)CLICK_LALLOC(nb_source_symbols * sizeof(uint8_t));
    // TODO: check if fail
    memset(x_to_source, 0, sizeof(uint8_t) * nb_source_symbols);
    memset(source_to_x, -1, sizeof(uint8_t) * nb_source_symbols);

    bool *protected_symbol = (bool *)CLICK_LALLOC(nb_source_symbols * sizeof(bool));
    // TODO: check if fail
    memset(protected_symbol, 0, sizeof(bool) * nb_source_symbols);

    uint32_t id_first_ss_first_window = encoding_symbol_id - nb_source_symbols + 1;
    uint32_t id_first_rs_first_window = id_first_ss_first_window + window_size - 1; // TODO: check this
    // click_chatter("\n----New processing----");
    click_chatter("Nb window=%u, id_first_ss=%u, first_rs=%u, nb_source=%u", nb_windows, id_first_ss_first_window, id_first_rs_first_window, nb_source_symbols);
    click_chatter("Window size=%u, encoding symbol id=%u, window_step=%u", window_size, encoding_symbol_id, window_step);
    // Locate the source and repair symbols and store separately
    uint8_t idx = id_first_rs_first_window;
    for (int i = 0; i < nb_windows; ++i) {
        srv6_fec2_repair_t *repair = _rlc_info.repair_buffer[idx % SRV6_FEC_BUFFER_SIZE];
        if (!repair) {
            click_chatter("ERROR 3");
            return; // TODO: free all
        }
        rs_array[i] = repair;
        // Update index for the next repair symbol
        idx += (repair->tlv.rfi >> 16) & 0xff;
    }
    for (int i = 0; i < nb_source_symbols; ++i) {
        idx = (id_first_ss_first_window + i) % SRV6_FEC_BUFFER_SIZE;
        srv6_fec2_source_t *source = _rlc_info.source_buffer[idx];
        uint32_t id_theoric = id_first_ss_first_window + i;
        bool is_lost = 0;
        if (source) {
            uint32_t id_from_buffer = source->tlv.sfpid;
            click_chatter("Source #%u: %x %x %x", idx, source->p->data()[0], source->p->data()[1], source->p->data()[2]);
            if (id_theoric == id_from_buffer && source->tlv.type == TLV_TYPE_FEC_SOURCE) {
                // Received symbol, store it in the buffer
                ss_array[i] = (srv6_fec2_source_term_t *)CLICK_LALLOC(sizeof(srv6_fec2_source_term_t));
                ss_array[i]->p = source->p;
                ss_array[i]->encoding_symbol_id = source->tlv.sfpid;
                max_packet_length = MAX(max_packet_length, source->p->length());
                // Bug occuring before that
                click_chatter("SSARRAY #%u: %x %x %x", idx, ss_array[i]->p->data()[0], ss_array[i]->p->data()[1], ss_array[i]->p->data()[2]);
            } else {
                is_lost = 1;
            }
        } else {
            is_lost = 1;
        }
        if (is_lost) { // Maybe this symbol was recovered earlier
            srv6_fec2_source_term_t *rec = _rlc_info.recovd_buffer[idx];
            if (rec) {
                if (rec->encoding_symbol_id == id_theoric) {
                    ss_array[i] = (srv6_fec2_source_term_t *)CLICK_LALLOC(sizeof(srv6_fec2_source_term_t));
                    ss_array[i]->p = rec->p;
                    ss_array[i]->encoding_symbol_id = rec->encoding_symbol_id;
                    max_packet_length = MAX(max_packet_length, rec->p->length());
                    
                    // Hence the packet is not lost anymore
                    is_lost = 0;
                }
            }
        }
        if (is_lost) {
            x_to_source[nb_unknwons] = i;
            source_to_x[i] = nb_unknwons;
            ++nb_unknwons;
            click_chatter("Lost symbol is: %u", id_theoric);
        }
    }

    //click_chatter("Nb windows: %u, nb_unk: %u", nb_windows, nb_unknwons);

    // Maybe no need for recovery?
    if (nb_unknwons == 0) {
        //click_chatter("No need for recovery! TODO: delete this message");
        
        // Free all memory
        for (int i = 0; i < nb_source_symbols; ++i) {
            if (ss_array[i]) CLICK_LFREE(ss_array[i], sizeof(srv6_fec2_source_term_t *));
        }
        CLICK_LFREE(ss_array, sizeof(srv6_fec2_source_term_t **) * nb_source_symbols);
        CLICK_LFREE(rs_array, sizeof(srv6_fec2_repair_t **) * nb_windows);
        CLICK_LFREE(x_to_source, sizeof(uint8_t) * nb_source_symbols);
        CLICK_LFREE(source_to_x, sizeof(uint8_t) * nb_source_symbols);
        CLICK_LFREE(protected_symbol, sizeof(bool) * nb_source_symbols);
        return;
    }

    //click_chatter("need recovery");

    // Construct the system Ax=b
    int n_eq = MIN(nb_unknwons, nb_windows);
    uint8_t *coefs = (uint8_t *)CLICK_LALLOC(sizeof(uint8_t) * window_size); // TODO: adaptive window size?
    srv6_fec2_term_t **unknowns = (srv6_fec2_term_t **)CLICK_LALLOC(sizeof(srv6_fec2_term_t *) * nb_unknwons); // x
    uint8_t **system_coefs = (uint8_t **)CLICK_LALLOC(sizeof(uint8_t *) * n_eq); // A
    srv6_fec2_term_t **constant_terms = (srv6_fec2_term_t **)CLICK_LALLOC(sizeof(srv6_fec2_term_t *) * nb_unknwons); // b
    bool *undetermined = (bool *)CLICK_LALLOC(sizeof(bool) * nb_unknwons);
    memset(coefs, 0, window_size * sizeof(uint8_t));
    memset(unknowns, 0, sizeof(srv6_fec2_term_t *) * nb_unknwons);
    memset(system_coefs, 0, sizeof(uint8_t *) * n_eq);
    memset(constant_terms, 0, sizeof(srv6_fec2_term_t *) * nb_unknwons);
    memset(undetermined, 0, sizeof(bool) * nb_unknwons);        
    
    for (int i = 0; i < n_eq; ++i) {
        system_coefs[i] = (uint8_t *)CLICK_LALLOC(sizeof(uint8_t) * nb_unknwons);
        memset(system_coefs[i], 0, sizeof(uint8_t) * nb_unknwons);
    }

    for (int i = 0; i < nb_unknwons; ++i) {
        unknowns[i] = (srv6_fec2_term_t *)CLICK_LALLOC(sizeof(srv6_fec2_term_t));
        memset(unknowns[i], 0, sizeof(srv6_fec2_term_t));
        unknowns[i]->data = (uint8_t *)CLICK_LALLOC(max_packet_length);
        memset(unknowns[i]->data, 0, sizeof(uint8_t) * max_packet_length);
    }

    int i = 0; // Index of the row in the system
    for (int rs = 0; rs < nb_windows; ++rs) {
        click_chatter("Start loop");
        //click_chatter("Passage 2: %u", rs);
        srv6_fec2_repair_t *repair = rs_array[rs];
        //click_chatter("Repair: %u (%u)", rs, repair->tlv.rfpid);
        bool protect_at_least_one = false;
        // Check if this repair symbol protects at least one lost source symbol
        // TODO: make adaptive
        for (int k = 0; k < window_size; ++k) {
            int idx = rs * window_step + k;
            if (!ss_array[idx] && !protected_symbol[idx]) {
                protect_at_least_one = true;
                protected_symbol[idx] = true;
                break; // We know it protects at least one
            } else if (ss_array[idx]){
                click_chatter("Source symbol: %u (idx=%u) from repair=%u (idx=%u)", ss_array[idx]->encoding_symbol_id, idx, repair->tlv.rfpid, rs);
            } else {
                click_chatter("Already protected symbol (idx=%d)", idx);
            }
        }
        click_chatter("Entering before construction of the system");
        // click_chatter("Have %u unk but %u for %u", nb_unknwons, protect_at_least_one, repair->tlv.rfpid);
        if (!protect_at_least_one) {
            continue; // Ignore this repair symbol if does not protect any lost
        }
        constant_terms[i] = (srv6_fec2_term_t *)CLICK_LALLOC(sizeof(srv6_fec2_term_t));

        // 1) Independent term (b) ith row
        memset(constant_terms[i], 0, sizeof(srv6_fec2_term_t));
        constant_terms[i]->data = (uint8_t *)CLICK_LALLOC(sizeof(uint8_t) * max_packet_length);
        memset(constant_terms[i]->data, 0, sizeof(uint8_t) * max_packet_length);
        click_ip6_sr *srv6 = reinterpret_cast<click_ip6_sr *>(repair->p->data() + sizeof(click_ip6));
        uint16_t repair_offset = sizeof(click_ip6) + sizeof(click_ip6_sr) + srv6->ip6_hdrlen * 8;
        memcpy(constant_terms[i]->data, repair->p->data() + repair_offset, repair->p->length() - repair_offset);
        click_chatter("Some first bytes of repir payload offset=%u", repair_offset);
        for (int kk = 0; kk < 5; ++kk) {
            fprintf(stderr, "%x ", constant_terms[i]->data[kk]);
        }
        fprintf(stderr, "\n");
        constant_terms[i]->packet_length = repair->tlv.coded_length;

        // 2) Coefficient matric (A) ith row
        uint16_t repair_key = repair->tlv.rfi & 0xffff;
        rlc_get_coefs(&prng, repair_key, window_size, coefs);
        // click_chatter("Repair key=%u (%u) and first 4 coefs= %u %u %u %u", repair_key, repair->tlv.rfpid, coefs[0], coefs[1], coefs[2], coefs[3]); 
        int current_unknown = 0; // Nb of unknown already discovered
        for (int j = 0; j < window_size; ++j) {
            int idx = rs * window_step + j;
            if (ss_array[idx]) { // This protected symbol is received
                click_chatter("Sub scaled %u (esid=%u) with coef=%u", j, ss_array[idx]->encoding_symbol_id, coefs[j]);
                click_chatter("First few bytes of it: %x %x %x", ss_array[idx]->p->data()[0], ss_array[idx]->p->data()[1], ss_array[idx]->p->data()[2]);
                symbol_sub_scaled_term(constant_terms[i], coefs[j], ss_array[idx], _rlc_info.muls);
            } else {
                if (source_to_x[idx] != -1) {
                    system_coefs[i][source_to_x[idx]] = coefs[j]; // A[i][j] = coefs[j]
                    ++current_unknown;
                    click_chatter("Lost symbol %u", idx);
                } else {
                    click_chatter("ERROR 4");
                }
            }
        }
        ++i;
        click_chatter("Done consturcting system here");
    }
    CLICK_LFREE(protected_symbol, sizeof(bool) * nb_source_symbols);
    uint8_t nb_effective_equations = i;

    // Print system to see what is the fucking problem
    for (int row = 0; row < n_eq; ++row) {
        for (int col = 0; col < nb_unknwons; ++col) {
            fprintf(stderr, "%u ", system_coefs[row][col]);
        }
        fprintf(stderr, "\n");
    }

    bool can_recover = nb_effective_equations >= nb_unknwons;
    if (can_recover) {
        // Solve the system
        click_chatter("Start gaussian elimination");
        gauss_elimination(nb_effective_equations, nb_unknwons, system_coefs, constant_terms, unknowns, undetermined, _rlc_info.muls, _rlc_info.table_inv, max_packet_length);
        click_chatter("Gaussian elimination done");
        uint8_t current_unknown = 0;
        int err = 0;
        for (int j = 0; j < nb_unknwons; ++j) {
            int idx = x_to_source[j];
            if (!ss_array[idx] && !undetermined[current_unknown] && !symbol_is_zero(unknowns[current_unknown]->data, max_packet_length)) {
                // New pointer for the recovered values
                srv6_fec2_source_term_t *recovered = (srv6_fec2_source_term_t *)CLICK_LALLOC(sizeof(srv6_fec2_source_term_t));
                memset(recovered, 0, sizeof(srv6_fec2_source_term_t));
                recovered->encoding_symbol_id = id_first_ss_first_window + idx;
                // Avoid stupid errors
                if (unknowns[current_unknown]->packet_length > max_packet_length) {
                    click_chatter("Recovered wrong packet size!");
                    CLICK_LFREE(recovered, sizeof(srv6_fec2_source_term_t));
                    continue;
                }
                click_chatter("Passage 3");
                WritablePacket *p_rec = recover_packet_fom_data(unknowns[current_unknown]->data, unknowns[current_unknown]->packet_length);
                if (!p_rec) {
                    click_chatter("Error from recovery confirmed");
                    continue;
                }
                click_chatter("Mothafucka");

                // Store a local copy of the packet for later recovery?
                recovered->p = p_rec->clone();

                // Send recovered packet on the line
                output(0).push(p_rec);

                // Store the recovered packet in the buffer and clean previous
                srv6_fec2_source_term_t *old_rec = _rlc_info.recovd_buffer[recovered->encoding_symbol_id % SRV6_FEC_BUFFER_SIZE];
                if (old_rec) {
                    click_chatter("Before freeing old red");
                    old_rec->p->kill();
                    CLICK_LFREE(old_rec, sizeof(srv6_fec2_source_term_t));
                    click_chatter("After freeing old rec");
                }
                click_chatter("Passage 2");
                _rlc_info.recovd_buffer[recovered->encoding_symbol_id % SRV6_FEC_BUFFER_SIZE] = recovered;
            }
            click_chatter("Passage 1");
            CLICK_LFREE(unknowns[current_unknown++], sizeof(srv6_fec2_term_t));
        }
    } else {
        click_chatter("ERROR 5: %u eq but %u unk", nb_effective_equations, nb_unknwons);
    }

    // Free the entire system
    for (i = 0; i < n_eq; ++i) {
        CLICK_LFREE(system_coefs[i], sizeof(uint8_t) * nb_unknwons);
        if (i < nb_effective_equations) {
            CLICK_LFREE(constant_terms[i], sizeof(srv6_fec2_term_t));
        }
    }
    for (i = 0; i < nb_source_symbols; ++i) {
        CLICK_LFREE(ss_array[i], sizeof(srv6_fec2_term_t));
    }
    CLICK_LFREE(ss_array, sizeof(srv6_fec2_term_t *) * nb_source_symbols);
    CLICK_LFREE(rs_array, sizeof(srv6_fec2_repair_t) * nb_windows);
    CLICK_LFREE(system_coefs, sizeof(uint8_t *) * n_eq);
    CLICK_LFREE(unknowns, sizeof(srv6_fec2_term_t *) * nb_unknwons);
    CLICK_LFREE(coefs, window_size * sizeof(uint8_t));
    CLICK_LFREE(undetermined, sizeof(bool) * nb_unknwons);
    CLICK_LFREE(source_to_x, sizeof(uint8_t) * nb_source_symbols);
    CLICK_LFREE(x_to_source, sizeof(uint8_t) * nb_source_symbols);
    return;
}

void
IP6SRv6FECDecode::symbol_add_scaled_term(srv6_fec2_term_t *symbol1, uint8_t coef, srv6_fec2_source_term_t *symbol2, uint8_t *mul)
{
    symbol_add_scaled(symbol1->data, coef, symbol2->p->data(), symbol2->p->length(), mul);
    uint16_t pl = (uint16_t)symbol2->p->length();
    symbol_add_scaled(&symbol1->packet_length, coef, &pl, sizeof(uint16_t), mul);
}

void
IP6SRv6FECDecode::symbol_add_scaled_term(srv6_fec2_term_t *symbol1, uint8_t coef, srv6_fec2_term_t *symbol2, uint8_t *mul)
{
    symbol_add_scaled(symbol1->data, coef, symbol2->data, symbol2->packet_length, mul);
    uint16_t pl = (uint16_t)symbol2->packet_length;
    symbol_add_scaled(&symbol1->packet_length, coef, &pl, sizeof(uint16_t), mul);
}

void
IP6SRv6FECDecode::symbol_mul_term(srv6_fec2_term_t *symbol1, uint8_t coef, uint8_t *mul, uint16_t size)
{
    symbol_mul(symbol1->data, coef, size, mul);
    symbol_mul((uint8_t *)&symbol1->packet_length, coef, sizeof(uint16_t), mul);
}

void
IP6SRv6FECDecode::swap(uint8_t **a, int i, int j) {
    uint8_t *tmp = a[j];
    a[j] = a[i];
    a[i] = tmp;
}

void
IP6SRv6FECDecode::swap_b(srv6_fec2_term_t **a, int i, int j)
{
    srv6_fec2_term_t *tmp = a[j];
    a[j] = a[i];
    a[i] = tmp;
}

int
IP6SRv6FECDecode::cmp_eq_i(uint8_t *a, uint8_t *b, int idx, int n_unknowns)
{
    if (a[idx] < b[idx]) return -1;
    else if (a[idx] > b[idx]) return 1;
    else if (a[idx] != 0) return 0;
    return 0;
}

int
IP6SRv6FECDecode::cmp_eq(uint8_t *a, uint8_t *b, int idx, int n_unknowns)
{
    for (int i = 0 ; i < n_unknowns; i++) {
        int cmp = 0;
        if ((cmp = cmp_eq_i(a, b, i, n_unknowns)) != 0) {
            return cmp;
        }
    }
    return 0;
}

void
IP6SRv6FECDecode::sort_system(uint8_t **a, srv6_fec2_term_t **constant_terms, int n_eq, int n_unknowns)
{
    for (int i = 0; i < n_eq; ++i) {
        int max = i;
        for (int j = i + 1; j < n_eq; ++j) {
            if (cmp_eq(a[max], a[j], i, n_unknowns) < 0) {
                max = j;
            }
        }
        swap(a, i, max);
        swap_b(constant_terms, i, max);
    }
}

int
IP6SRv6FECDecode::first_non_zero_idx(const uint8_t *a, int n_unknowns) {
    for (int i = 0 ; i < n_unknowns ; i++) {
        if (a[i] != 0) {
            return i;
        }
    }
    return -1;
}

void
IP6SRv6FECDecode::gauss_elimination(int n_eq, int n_unknowns, uint8_t **a, srv6_fec2_term_t **constant_terms, srv6_fec2_term_t **x, bool *undetermined, uint8_t *mul, uint8_t *inv, uint16_t max_packet_length)
{
    sort_system(a, constant_terms, n_eq, n_unknowns);
    for (int i = 0; i < n_eq - 1; ++i) {
        for (int k = i + 1; k < n_eq; ++k) {
            uint8_t mul_num = a[k][i];
            uint8_t mul_den = a[i][i];
            uint8_t term = gf256_mul(mul_num, inv[mul_den], mul);
            for (int j = 0; j < n_unknowns; ++j) {
                a[k][j] = gf256_sub(a[k][j], gf256_mul(term, a[i][j], mul));
            }
            symbol_sub_scaled_term(constant_terms[k], term, constant_terms[i], mul);
        }
    }

    sort_system(a, constant_terms, n_eq, n_unknowns);

    for (int i = 0; i < n_eq - 1; ++i) {
        int first_nz_id = first_non_zero_idx(a[i], n_unknowns);
        if (first_nz_id == -1) {
            break;
        }
        for (int j = first_nz_id + 1; j < n_unknowns && a[i][j] != 0; j++) {
            for (int k = i + 1; k < n_eq; k++) {
                int first_nz_id_below = first_non_zero_idx(a[k], n_unknowns);
                if (j > first_nz_id_below) {
                    break;
                } else if (first_nz_id_below == j) {
                    uint8_t term = gf256_mul(a[i][j], inv[a[k][j]], mul);
                    for (int l = j; l < n_unknowns; l++) {
                        a[i][l] = gf256_sub(a[i][l], gf256_mul(term, a[k][l], mul));
                    }
                    symbol_sub_scaled_term(constant_terms[i], term, constant_terms[k], mul);
                    break;
                }
            }
        }
    }

    int candidate = n_unknowns - 1;
    for (int i = n_eq - 1; i >= 0; --i) {
        bool only_zeroes = true;
        for (int j = 0; j < n_unknowns; ++j) {
            if (a[i][j] != 0) {
                only_zeroes = false;
                break;
            }
        }
        if (!only_zeroes) {
            while (a[i][candidate] == 0 && candidate >= 0) {
                undetermined[candidate--] = true;
            }
            if (candidate < 0) {
                fprintf(stderr, "System partially undetermined\n");
                break;
            }
            memcpy(x[candidate], constant_terms[i], sizeof(srv6_fec2_term_t));
            for (int j = 0; j < candidate; ++j) {
                if (a[i][j] != 0) {
                    undetermined[candidate] = true;
                    break;
                }
            }
            for (int j = candidate + 1; j < n_unknowns; ++j) {
                if (a[i][j] != 0) {
                    if (undetermined[j]) {
                        undetermined[candidate] = true;
                    } else {
                        symbol_sub_scaled_term(x[candidate], a[i][j], x[j], mul);
                        a[i][j] = 0;
                    }
                }
            }
            if (symbol_is_zero(x[candidate]->data, max_packet_length) || a[i][candidate] == 0) {
                undetermined[candidate] = true;
            } else if (!undetermined[candidate]) {
                symbol_mul_term(x[candidate], inv[a[i][candidate]], mul, max_packet_length);
                a[i][candidate] = gf256_mul(a[i][candidate], inv[a[i][candidate]], mul);
            } 
            candidate--;
        }
    }
    if (candidate >= 0) {
        memset(undetermined, true, (candidate + 1) * sizeof(bool));
    }
}

WritablePacket *
IP6SRv6FECDecode::recover_packet_fom_data(uint8_t *data, uint16_t packet_length)
{
    // Create new packet for the recovered data
    WritablePacket *p = Packet::make(packet_length);

    // Copy the data from the buffer inside the new packet
    // TODO: optimization: direclty un a Packet ?
    click_chatter("Packet length of recovered: %u", packet_length);
    memcpy(p->data(), data, packet_length);

    // Loop at the packet content a bit
    for (int i = 0; i < 20; ++i) {
        fprintf(stderr, "%x ", p->data()[i]);
    }
    fprintf(stderr, "\n");
    return 0; // TODO: remove this

    // Recover from varying fields
    click_ip6 *ip6 = reinterpret_cast<click_ip6 *>(p->data());
    click_ip6_sr *srv6 = reinterpret_cast<click_ip6_sr *>(p->data() + sizeof(click_ip6));
    // 1. Detect the correct new next hop
    uint32_t *dec_ip_32 = dec.data32();
    bool found_next_sid = false;
    struct in6_addr ip6_next;
    int i = srv6->last_entry;
    click_chatter("Before SRH look, i=%u", i);
    while (i >= 0) {
        uint16_t offset = sizeof(click_ip6_sr) + sizeof(struct in6_addr) * i;
        memcpy(&ip6_next, srv6 + offset, sizeof(struct in6_addr));
        // Compare this SID with the decoder ID
        uint32_t *in6_32 = (uint32_t *)&ip6_next;
        if (dec_ip_32[0] == in6_32[0] && dec_ip_32[1] == in6_32[1] && dec_ip_32[2] == in6_32[2] && dec_ip_32[3] == in6_32[3]) {
            found_next_sid = true;
            break;
        }
        --i;
    }
    click_chatter("After SRH look");
    if (!found_next_sid || i == 0) {
        click_chatter("Did not find the SID => error");
        p->kill();
        return 0;
    }

    --i; // Next segment is the next hop
    uint16_t offset = sizeof(click_ip6_sr) + sizeof(struct in6_addr) * i;

    // 2. Replace the destination address with the correct
    memcpy(&ip6->ip6_dst, srv6 + offset, sizeof(struct in6_addr));

    // 3. Replace the Segment Left pointer
    srv6->segment_left = i - 1;

    // 4. New hop limit
    ip6->ip6_hlim = 51;

    // 5. Remove possible ECN bits
    ip6->ip6_vfc &= 0b11111100;

    // 6. Compute the checksum
    // TODO
    click_chatter("After all");

    return p;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IP6SRv6FECDecode)
ELEMENT_MT_SAFE(IP6SRv6FECDecode)

