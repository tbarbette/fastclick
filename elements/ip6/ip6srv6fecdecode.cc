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
    for (int i = 0; i < SRV6_FEC_BUFFER_SIZE; ++i) {
        srv6_fec2_source_t *packet = _rlc_info.source_buffer[i];
        if (packet) {
            packet->p->kill();
            CLICK_LFREE(packet, sizeof(srv6_fec2_source_t));
        }
        srv6_fec2_repair_t *repair = _rlc_info.repair_buffer[i];
        if (repair) {
            repair->p->kill();
            CLICK_LFREE(repair, sizeof(srv6_fec2_repair_t));
        }
        srv6_fec2_source_t *recovered = _rlc_info.recovd_buffer[i];
        if (recovered) {
            recovered->p->kill();
            CLICK_LFREE(recovered, sizeof(srv6_fec2_source_t));
        }
    }
}

int
IP6SRv6FECDecode::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
	.read_mp("ENC", enc)
	.read_mp("DEC", dec)
	.read_or_set("RECOVER", _do_recover, true)
	.complete() < 0)
        return -1;
    return 0;
}


void
IP6SRv6FECDecode::push(int, Packet *p_in)
{
    fec_framework(p_in, [this](Packet*p){output(0).push(p);});
}

#if HAVE_BATCH
void 
IP6SRv6FECDecode::push_batch(int, PacketBatch *batch) {
    EXECUTE_FOR_EACH_PACKET_ADD( fec_framework, batch );
    if (batch) {
        output_push_batch(0, batch);
    }

}
#endif

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
IP6SRv6FECDecode::fec_framework(Packet *p_in, std::function<void(Packet*)>push)
{
    // Manipulate modified packet because we will remove the TLV
    WritablePacket *p = p_in->uniqueify();
    if (unlikely(!p)) {
        click_chatter("oom!");
        return;
    }
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
        push(p);
        return;
    }

    if (tlv_type == TLV_TYPE_FEC_SOURCE) {
        // Load TLV locally
        source_tlv_t source_tlv;
        memset(&source_tlv, 0, sizeof(source_tlv_t));
        memcpy(&source_tlv, tlv_ptr, sizeof(source_tlv_t));
        
        // Remove the TLV from the source packet
        remove_tlv_source_symbol(p, tlv_ptr - p->data()); // Cleaner way?

        // Call FEC Scheme
        fec_scheme_source(p, &source_tlv);
    } else {
        // Load TLV locally
        repair_tlv_t repair_tlv;
        memset(&repair_tlv, 0, sizeof(repair_tlv_t));
        memcpy(&repair_tlv, tlv_ptr, sizeof(repair_tlv_t));

        // Call FEC Scheme
        Packet* p_rec = fec_scheme_repair(p, &repair_tlv);
	if (p_rec)
		push(p_rec);
    }

    // Send the (modified, without TLV) source symbol
    // i.e., do not send the repair symbol out of the tunnel
    if (tlv_type == TLV_TYPE_FEC_SOURCE) {
        push(p);
    }
}

int
IP6SRv6FECDecode::fec_scheme_source(WritablePacket *p_in, source_tlv_t *tlv)
{
    // Store packet as source symbol
    store_source_symbol(p_in, tlv);

    return 0;
}

Packet*
IP6SRv6FECDecode::fec_scheme_repair(WritablePacket *p_in, repair_tlv_t *tlv)
{

    // Store packet as source symbol
    store_repair_symbol(p_in, tlv);

    Packet* p;

    // Call RLC recovery
    if (tlv->padding == SRV6_FEC_RLC) {
        p = rlc_recover_symbols();
    } else {
        p = xor_recover_symbols();
    }

    return p;
}

void
IP6SRv6FECDecode::store_source_symbol(WritablePacket *p_in, source_tlv_t *tlv) {
    uint32_t encoding_symbol_id = tlv->sfpid;
    // Store the source symbol
    srv6_fec2_source_t *symbol = (srv6_fec2_source_t *)CLICK_LALLOC(sizeof(srv6_fec2_source_t));
    // TODO: check if call failed?
    symbol->encoding_symbol_id = tlv->sfpid;
    symbol->p = p_in->clone();

    // Clean previous symbol in the buffer and replace with current symbol
    srv6_fec2_source_t *previous_symbol = _rlc_info.source_buffer[encoding_symbol_id % SRV6_FEC_BUFFER_SIZE];
    if (previous_symbol) {
	assert(previous_symbol->p);
        previous_symbol->p->kill();
        CLICK_LFREE(previous_symbol, sizeof(srv6_fec2_source_t));
    }
    _rlc_info.source_buffer[encoding_symbol_id % SRV6_FEC_BUFFER_SIZE] = symbol;

    // Update the feedback string bit
    // _rlc_feedback.received_string |= (1 << _rlc_feedback.nb_received++);
}

void
IP6SRv6FECDecode::store_repair_symbol(WritablePacket *p_in, repair_tlv_t *tlv)
{
    uint32_t encoding_symbol_id = tlv->rfpid;

    // Store the repair symbol
    srv6_fec2_repair_t *symbol = (srv6_fec2_repair_t *)CLICK_LALLOC(sizeof(srv6_fec2_repair_t));
    // TODO: check if call failed?
    memset(symbol, 0, sizeof(srv6_fec2_repair_t));
    memcpy(&symbol->tlv, tlv, sizeof(repair_tlv_t));
    symbol->p = p_in->clone(); // TODO: see if correct with Tom
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
    unsigned len = p->network_header_length();
    click_ip6 *ip6 = reinterpret_cast<click_ip6 *>(p->data());
    click_ip6_sr *srv6 = reinterpret_cast<click_ip6_sr *>(p->data() + sizeof(click_ip6));
    ip6->ip6_plen -= htons(sizeof(source_tlv_t));
    srv6->ip6_hdrlen -= 1;

    // Push everything before the TLV, sizeof(tlv) after
    memmove(p->data() + sizeof(source_tlv_t), p->data(), offset_tlv);
    p->pull(sizeof(source_tlv_t));
    p->set_network_header(p->data(), len - sizeof(source_tlv_t));
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

srv6_fec2_term_t *
IP6SRv6FECDecode::init_term(Packet *p, uint16_t offset, uint16_t max_packet_length)
{
    srv6_fec2_term_t *t = (srv6_fec2_term_t *)CLICK_LALLOC(sizeof(srv6_fec2_term_t));
    uint8_t *data = (uint8_t *)CLICK_LALLOC(sizeof(uint8_t) * max_packet_length);
    memcpy(data, p->data() + offset, p->length() - offset);
    t->data = data;
    t->length.coded_length = 0;
    return t;
}
void kill_term(srv6_fec2_term_t *t)
{
    CLICK_LFREE(t->data, t->length.data_length);
    CLICK_LFREE(t, sizeof(srv6_fec2_term_t));
}

Packet*
IP6SRv6FECDecode::rlc_recover_symbols()
{
    uint16_t max_packet_length = 0; // decoding size
    uint8_t nb_source_symbols = 0;
    uint8_t window_size = 0;
    uint8_t window_step = 0;
    uint8_t previous_window_step = 0;
    uint8_t max_seen_window_size = 0;
    uint32_t encoding_symbol_id = _rlc_info.encoding_symbol_id;
    WritablePacket *p_rec = 0;

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
        const click_ip6_sr *srv6 = reinterpret_cast<const click_ip6_sr *>(repair->p->data() + sizeof(click_ip6));
        uint16_t repair_offset = sizeof(click_ip6) + sizeof(click_ip6_sr) + srv6->ip6_hdrlen * 8;
        max_packet_length = MAX(max_packet_length, repair->p->length() - repair_offset);
        window_size = repair->tlv.nss;
        max_seen_window_size = MAX(max_seen_window_size, window_size);
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
    nb_source_symbols += window_size - previous_window_step; // TODO: verify this: LGTM

    // No valid window: no repair symbol received or no FEC applied
    // Should not happen since this function is triggered by the
    // reception of a repair symbol
    if (unlikely(nb_windows == 0)) {
        click_chatter("Should not happen empty window");
        return 0;
    }

    // Received source symbols array
    srv6_fec2_source_t **ss_array = (srv6_fec2_source_t **)CLICK_LALLOC(sizeof(srv6_fec2_source_t *) * nb_source_symbols);
    if (unlikely(!ss_array)) {
        return 0;
    }
    memset(ss_array, 0, sizeof(srv6_fec2_source_t *) * nb_source_symbols);

    // Received repair symbols array
    srv6_fec2_repair_t **rs_array = (srv6_fec2_repair_t **)CLICK_LALLOC(sizeof(srv6_fec2_repair_t *) * nb_windows);
    if (unlikely(!rs_array)) {
        CLICK_LFREE(ss_array, sizeof(srv6_fec2_source_t *) * nb_source_symbols);
        return 0;
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
    uint32_t id_first_rs_first_window = id_first_ss_first_window + window_size - 1; // TODO: check this: LGTM
    // Locate the source and repair symbols and store separately
    uint8_t idx = id_first_rs_first_window;
    for (int i = 0; i < nb_windows; ++i) {
        srv6_fec2_repair_t *repair = _rlc_info.repair_buffer[idx % SRV6_FEC_BUFFER_SIZE];
        if (!repair) {
            click_chatter("ERROR 3");
            return 0; // TODO: free all
        }
        rs_array[i] = repair;
        // Update index for the next repair symbol
        idx += (repair->tlv.rfi >> 16) & 0xff;
        // click_chatter("RFI=%x, window_step=%u, window_step=%u", repair->tlv.rfi, (repair->tlv.rfi >> 16) & 0xff, repair->tlv.rlc_rfi.window_step);
    }
    for (int i = 0; i < nb_source_symbols; ++i) {
        idx = (id_first_ss_first_window + i) % SRV6_FEC_BUFFER_SIZE;
        srv6_fec2_source_t *source = _rlc_info.source_buffer[idx];
        uint32_t id_theoric = id_first_ss_first_window + i;
        bool is_lost = 0;
        if (source) {
            uint32_t id_from_buffer = source->encoding_symbol_id;
            if (id_theoric == id_from_buffer) {
                // Received symbol, store it in the buffer
                ss_array[i] = source;
            } else {
                is_lost = 1;
            }
        } else {
            is_lost = 1;
        }
        if (is_lost) { // Maybe this symbol was recovered earlier and stored in recovered buffer
            srv6_fec2_source_t *rec = _rlc_info.recovd_buffer[idx];
            if (rec) {
                if (rec->encoding_symbol_id == id_theoric) {
                    ss_array[i] = rec;
                    
                    // Hence the packet is not lost anymore
                    is_lost = 0;
                }
            }
        }
        if (is_lost) {
            x_to_source[nb_unknwons] = i;
            source_to_x[i] = nb_unknwons;
            ++nb_unknwons;
        }
    }

    // Maybe no need for recovery?
    if (nb_unknwons == 0) {
        // Free all memory
        // We do not free the memory of the cells in ss_array because these are pointers
        // to elements of the source and recovered buffers
        CLICK_LFREE(ss_array, sizeof(srv6_fec2_source_t *) * nb_source_symbols);
        CLICK_LFREE(rs_array, sizeof(srv6_fec2_repair_t *) * nb_windows);
        CLICK_LFREE(x_to_source, sizeof(uint8_t) * nb_source_symbols);
        CLICK_LFREE(source_to_x, sizeof(uint8_t) * nb_source_symbols);
        CLICK_LFREE(protected_symbol, sizeof(bool) * nb_source_symbols);
        return 0;
    }

    // Construct the system Ax=b
    int n_eq = MIN(nb_unknwons, nb_windows);
    uint8_t *coefs = (uint8_t *)CLICK_LALLOC(sizeof(uint8_t) * max_seen_window_size); // DONE: adaptive window size: done, now maximum window size
    // TODO: directly make packets with maximum length ? See if legit but would be faster
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

    int i = 0; // Index of the row in the system
    for (int rs = 0; rs < nb_windows; ++rs) {
        srv6_fec2_repair_t *repair = rs_array[rs];
        uint8_t this_window_size = repair->tlv.nss;
        uint32_t this_encoding_symbol_id = repair->tlv.rfpid;
        bool protect_at_least_one = false;
        // Check if this repair symbol protects at least one lost source symbol
        // the following seems correct
        int idx = this_encoding_symbol_id - id_first_ss_first_window - this_window_size + 1; // TODO: check if correct
        for (int k = 0; k < this_window_size; ++k) {
            if (!ss_array[idx + k] && !protected_symbol[idx + k]) {
                protect_at_least_one = true;
                protected_symbol[idx + k] = true;
                break; // We know it protects at least one
            }
        }
        if (!protect_at_least_one) {
            continue; // Ignore this repair symbol if does not protect any lost
        }

        // 1) Independent term (b) ith row
        const click_ip6_sr *srv6 = reinterpret_cast<const click_ip6_sr *>(repair->p->data() + sizeof(click_ip6));
        uint16_t repair_offset = sizeof(click_ip6) + sizeof(click_ip6_sr) + srv6->ip6_hdrlen * 8;
        constant_terms[i] = init_term(repair->p, repair_offset, max_packet_length);
        constant_terms[i]->length.coded_length = repair->tlv.coded_length;

        // 2) Coefficient matrix (A) ith row
        uint16_t repair_key = repair->tlv.rlc_rfi.repair_key; // TODO
        // rlc_get_coefs(&prng, repair_key, this_window_size, coefs);
        rlc_get_coefs(&prng, 1, this_window_size, coefs);
        int current_unknown = 0; // Nb of unknown already discovered
        idx = this_encoding_symbol_id - id_first_ss_first_window - this_window_size + 1; // TODO: check if correct
        for (int j = 0; j < this_window_size; ++j) {
            int idx_this_ss = idx + j; // Index of location of this source symbol
            if (ss_array[idx_this_ss]) { // This protected symbol is received
                // print_packet(ss_array[idx]);
                symbol_sub_scaled_term(constant_terms[i], coefs[j], ss_array[idx_this_ss], _rlc_info.muls);
            } else {
                if (source_to_x[idx_this_ss] != -1) {
                    system_coefs[i][source_to_x[idx_this_ss]] = coefs[j]; // A[i][j] = coefs[j]
                    ++current_unknown;
                } else {
                    click_chatter("ERROR 4");
                }
            }
        }
        ++i;
    }
    CLICK_LFREE(protected_symbol, sizeof(bool) * nb_source_symbols);
    uint8_t nb_effective_equations = i;

    // Print system to see what is the fucking problem
    // for (int row = 0; row < n_eq; ++row) {
    //     for (int col = 0; col < nb_unknwons; ++col) {
    //         fprintf(stderr, "%u ", system_coefs[row][col]);
    //     }
    //     fprintf(stderr, "\n");
    // }

    bool can_recover = nb_effective_equations >= nb_unknwons;
    if (can_recover && _do_recover) {
        // Solve the system
        gauss_elimination(nb_effective_equations, nb_unknwons, system_coefs, constant_terms, unknowns, undetermined, _rlc_info.muls, _rlc_info.table_inv, max_packet_length);
        uint8_t current_unknown = 0;
        int err = 0;
        for (int j = 0; j < nb_unknwons; ++j) {
            int idx = x_to_source[j];
            if (!ss_array[idx] && !undetermined[current_unknown]) {
                // Avoid stupid errors
                if (unknowns[current_unknown]->length.data_length > max_packet_length) {
                    click_chatter("Wrong size");
                    continue;
                }
                // Packet from the recovered data
                p_rec = recover_packet_fom_data(unknowns[current_unknown]);
                if (!p_rec) {
                    click_chatter("Error from recovery confirmed");
                    continue;
                };
                
                // New pointer for the recovered values
                srv6_fec2_source_t *recovered = (srv6_fec2_source_t *)CLICK_LALLOC(sizeof(srv6_fec2_source_t));
                recovered->encoding_symbol_id = id_first_ss_first_window + idx;

                // Store a local copy of the packet for later recovery?
                recovered->p = p_rec->clone();

                // Store the recovered packet in the buffer and clean previous
                srv6_fec2_source_t *old_rec = _rlc_info.recovd_buffer[recovered->encoding_symbol_id % SRV6_FEC_BUFFER_SIZE];
                if (old_rec) {
                    old_rec->p->kill();
                    CLICK_LFREE(old_rec, sizeof(srv6_fec2_source_t));
                }
                _rlc_info.recovd_buffer[recovered->encoding_symbol_id % SRV6_FEC_BUFFER_SIZE] = recovered;
            }
            ++current_unknown;
        }
    }

    // Free the entire system
    for (i = 0; i < n_eq; ++i) {
        CLICK_LFREE(system_coefs[i], sizeof(uint8_t) * nb_unknwons);
    }
    for (int i = 0; i < nb_unknwons; ++i) {
        if (constant_terms[i]) {
            CLICK_LFREE(constant_terms[i]->data, max_packet_length);
        }
    }
    CLICK_LFREE(ss_array, sizeof(srv6_fec2_source_t *) * nb_source_symbols);
    CLICK_LFREE(rs_array, sizeof(srv6_fec2_repair_t *) * nb_windows);
    CLICK_LFREE(system_coefs, sizeof(uint8_t *) * n_eq);
    CLICK_LFREE(unknowns, sizeof(srv6_fec2_term_t *) * nb_unknwons);
    CLICK_LFREE(coefs, sizeof(uint8_t) * max_seen_window_size); // Changed to max_seen_window_size
    CLICK_LFREE(undetermined, sizeof(bool) * nb_unknwons);
    CLICK_LFREE(source_to_x, sizeof(uint8_t) * nb_source_symbols);
    CLICK_LFREE(x_to_source, sizeof(uint8_t) * nb_source_symbols);
    return p_rec;
}

void
IP6SRv6FECDecode::symbol_add_scaled_term(srv6_fec2_term_t *symbol1, uint8_t coef, srv6_fec2_source_t *symbol2, uint8_t *mul)
{
    symbol_add_scaled(symbol1->data, coef, symbol2->p->data(), symbol2->p->length(), mul);
    uint16_t pl = (uint16_t)symbol2->p->length();
    symbol_add_scaled(&symbol1->length.coded_length, coef, &pl, sizeof(uint16_t), mul);
}

void
IP6SRv6FECDecode::symbol_add_scaled_term(srv6_fec2_term_t *symbol1, uint8_t coef, srv6_fec2_term_t *symbol2, uint8_t *mul, uint16_t decoding_size)
{
    symbol_add_scaled(symbol1->data, coef, symbol2->data, decoding_size, mul);
    uint16_t pl = (uint16_t)symbol2->length.coded_length;
    symbol_add_scaled(&symbol1->length.coded_length, coef, &pl, sizeof(uint16_t), mul);
}

void
IP6SRv6FECDecode::symbol_mul_term(srv6_fec2_term_t *symbol1, uint8_t coef, uint8_t *mul, uint16_t size)
{
    symbol_mul(symbol1->data, coef, size, mul);
    symbol_mul((uint8_t *)&symbol1->length.coded_length, coef, sizeof(uint16_t), mul);
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
            symbol_sub_scaled_term(constant_terms[k], term, constant_terms[i], mul, max_packet_length);
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
                    symbol_sub_scaled_term(constant_terms[i], term, constant_terms[k], mul, max_packet_length);
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
                break;
            }
            // TODO: not optimal because of aliasing
            x[candidate] = constant_terms[i]; // Simply pointer copy
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
                        symbol_sub_scaled_term(x[candidate], a[i][j], x[j], mul, max_packet_length);
                        a[i][j] = 0;
                    }
                }
            }
            if (symbol_is_zero(x[candidate]->data, x[candidate]->length.data_length) || a[i][candidate] == 0) {
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
IP6SRv6FECDecode::recover_packet_fom_data(srv6_fec2_term_t *rec)
{
    // Create new packet for the recovered data
    WritablePacket *p = Packet::make(rec->length.data_length);

    // Copy the data from the buffer inside the new packet
    // TODO: optimization: direclty un a Packet ?
    memcpy(p->data(), rec->data, rec->length.data_length);

    // Recover from varying fields
    click_ip6 *ip6 = reinterpret_cast<click_ip6 *>(p->data());
    click_ip6_sr *srv6 = reinterpret_cast<click_ip6_sr *>(p->data() + sizeof(click_ip6));
    // 1. Detect the correct new next hop
    uint32_t *dec_ip_32 = dec.data32();
    bool found_next_sid = false;
    struct in6_addr ip6_next;
    int i = srv6->last_entry;
    uint8_t *dec_8 = (uint8_t *)dec_ip_32;
    while (i >= 0) {
        uint16_t offset = sizeof(click_ip6_sr) + sizeof(struct in6_addr) * i;
        memcpy(&ip6_next, ((uint8_t *)srv6) + offset, sizeof(struct in6_addr));
        // Compare this SID with the decoder ID
        uint32_t *in6_32 = (uint32_t *)ip6_next.s6_addr;
        uint8_t *en_8 = (uint8_t *)&ip6_next;
        if (dec_ip_32[0] == in6_32[0] && dec_ip_32[1] == in6_32[1] && dec_ip_32[2] == in6_32[2] && dec_ip_32[3] == in6_32[3]) {
            found_next_sid = true;
//            click_chatter("I HAVE FOUND THE SID");
            break;
        }
        --i;
    }
    if (!found_next_sid || i == 0) {
        click_chatter("Did not find the SID => error");
        p->kill();
        return 0;
    }

    --i; // Next segment is the next hop
    uint16_t offset = sizeof(click_ip6_sr) + sizeof(struct in6_addr) * i;

    // 2. Replace the destination address with the correct
    memcpy(&ip6->ip6_dst, ((uint8_t *)srv6) + offset, sizeof(struct in6_addr));

    // 3. Replace the Segment Left pointer
    srv6->segment_left = i;

    // 4. New hop limit
    ip6->ip6_hlim = 51;

    // 5. Remove possible ECN bits
    ip6->ip6_vfc &= 0b11111100;

    // 6. Compute the checksum
    // TODO : utiliser un element si besoin

    // 7. Set annotations
    p->set_network_header((unsigned char*)ip6, sizeof(click_ip6) + sizeof(click_ip6_sr) + sizeof(struct in6_addr) * srv6->last_entry + 8 );

    return p;
}

Packet*
IP6SRv6FECDecode::xor_recover_symbols()
{
    uint32_t esid = _rlc_info.encoding_symbol_id;
    srv6_fec2_repair_t *repair = _rlc_info.repair_buffer[esid % SRV6_FEC_BUFFER_SIZE];
    uint8_t window_size = repair->tlv.nss;
    const click_ip6_sr *srv6 = reinterpret_cast<const click_ip6_sr *>(repair->p->data() + sizeof(click_ip6));
    uint16_t repair_offset = sizeof(click_ip6) + sizeof(click_ip6_sr) + srv6->ip6_hdrlen * 8;
    uint16_t max_packet_length = repair->p->length() - repair_offset;

    // 1. Detect if we can recover a lost source symbol in the window
    //    If there are more than one lost symbol in the window, we cannot recover it
    //    Store them in a separate buffer for easier access
    Packet *xor_buff[window_size];
    uint8_t nb_source = 0;
    bool lost_one_symbol = false;
    uint32_t lost_esid = 0;
    for (uint32_t i = 0; i < window_size; ++i) {
        // Iterate from the end but XOR is commutative and associative
        uint16_t idx = (esid - i) % SRV6_FEC_BUFFER_SIZE;
        uint32_t theoric_esid = esid - i;
        srv6_fec2_source_t *source = _rlc_info.source_buffer[idx];
        srv6_fec2_source_t *rec = _rlc_info.recovd_buffer[idx];
        if (source && source->encoding_symbol_id == theoric_esid) {
            xor_buff[nb_source++] = source->p;
        } else if (rec && rec->encoding_symbol_id == theoric_esid) {
            // Maybe the symbol was recovered earlier in a previous window
            xor_buff[nb_source++] = rec->p;
        } else {
            if (lost_one_symbol) {
                return 0; // More than one lost symbol, cannot recover
            }
            lost_one_symbol = true;
            lost_esid = theoric_esid;
        }
    }

    if (!lost_one_symbol) {
        return 0;
    }

    // 2. We know we have lost exactly one source symbol
    //    We can recover it by XORing the repair and source symbols
    srv6_fec2_term_t *rec = (srv6_fec2_term_t *)CLICK_LALLOC(sizeof(srv6_fec2_term_t));
    uint8_t *data = (uint8_t *)CLICK_LALLOC(sizeof(uint8_t) * max_packet_length);
    rec->length.coded_length = repair->tlv.coded_length;
    rec->data = data;
    // Copy data from the repair symbol
    memcpy(data, repair->p->data() + repair_offset, max_packet_length);
    for (uint32_t i = 0; i < nb_source; ++i) {
        xor_one_symbol(rec, xor_buff[i]);
    }

    // 3. Send the recovered symbol and store it in the recovered buffer
    //    Also make room if there was a previous recovered buffer
    WritablePacket *p_rec = recover_packet_fom_data(rec);
    if (!p_rec) {
        click_chatter("Error confirmed");
        CLICK_LFREE(rec, sizeof(srv6_fec2_term_t));
        return 0;
    }
    srv6_fec2_source_t *prev_rec = _rlc_info.recovd_buffer[lost_esid % SRV6_FEC_BUFFER_SIZE];
    if (prev_rec) {
        prev_rec->encoding_symbol_id = lost_esid;
        prev_rec->p = p_rec->clone();
    } else {
        srv6_fec2_source_t *rec = (srv6_fec2_source_t *)CLICK_LALLOC(sizeof(srv6_fec2_source_t));
        rec->encoding_symbol_id = lost_esid;
        rec->p = p_rec->clone();
        _rlc_info.recovd_buffer[lost_esid % SRV6_FEC_BUFFER_SIZE] = rec;
    }
    
    CLICK_LFREE(data, sizeof(uint8_t) * max_packet_length);
    CLICK_LFREE(rec, sizeof(srv6_fec2_term_t));

    return p_rec;
}

void
IP6SRv6FECDecode::xor_one_symbol(srv6_fec2_term_t *rec, Packet *s)
{
    uint8_t *s_64 = (uint8_t *)s->data();
    uint8_t *r_64 = (uint8_t *)rec->data;

    for (uint16_t i = 0; i < s->length() / sizeof(uint8_t); ++i) {
        // click_chatter("XOR with source i=%u  %x, repair before=%x", i, s_64[i], r_64[i]);
        r_64[i] ^= s_64[i];
    }

    // Also code the potential remaining data
    uint8_t *s_8 = (uint8_t *)s->data();
    uint8_t *r_8 = (uint8_t *)rec->data;
    for (uint16_t i = (s->length() / sizeof(uint8_t)) * sizeof(uint8_t); i < s->length(); ++i) {
        r_8[i] ^= s_8[i];
    }

    // Encode the packet length
    rec->length.coded_length ^= s->length();
}

// void
// IP6SRv6FECDecode::rlc_feedback()
// {
//     uint16_t packet_size = sizeof(click_ip6) + sizeof(click_ip6_sr) + 2 * sizeof(IP6Address) + sizeof(feedback_tlv_t);
//     WritablePacket *p = Packet::make(packet_size);
//     if (!p) {
//         return;
//     }

//     click_ip6 *ip6 = reinterpret_cast<click_ip6 *>(p->data());
//     click_ip6_sr *srv6 = reinterpret_cast<click_ip6_sr *>(p->data() + sizeof(click_ip6));
//     uint8_t *segment_list = ((uint8_t *)srv6) + sizeof(click_ip6_sr);
//     feedback_tlv_t *tlv = reinterpret_cast<feedback_tlv_t *>(p->data() + sizeof(click_ip6) + sizeof(click_ip6_sr) + sizeof(IP6Address) * 2);

//     // IPv6 Header
//     memcpy(&ip6->ip6_src, dec.data(), sizeof(IP6Address));
//     memcpy(&ip6->ip6_dst, enc.data(), sizeof(IP6Address));
//     ip6->ip6_flow = htonl(6 << IP6_V_SHIFT);
//     ip6->ip6_plen = packet_size - sizeof(click_ip6);
//     ip6->ip6_nxt = IPPROTO_ROUTING;
//     ip6->ip6_hlim = 51;

//     // SRv6 Header
//     srv6->type = IP6PROTO_SEGMENT_ROUTING;
//     srv6->segment_left = 1;
//     srv6->last_entry = 1;
//     srv6->flags = 0;
//     srv6->tag = 0;
//     srv6->ip6_sr_next = 253;
//     srv6->ip6_hdrlen = (sizeof(feedback_tlv_t) + 2 * sizeof(IP6Address)) / 8;
//     memcpy(&srv6->segments[0], enc.data(), sizeof(IP6Address));
//     memcpy(&srv6->segments[1], dec.data(), sizeof(IP6Address));

//     // Add feedback TLV
//     tlv->type = TLV_TYPE_FEC_FEEDBACK;
//     tlv->len = sizeof(feedback_tlv_t) - 2;
//     tlv->padding16 = 0;
//     tlv->padding32 = 0;
//     // tlv->bit_string = _rlc_feedback.received_string;

//     // Set annotations
//     p->set_network_header(p->data(), (unsigned char*)(tlv + 1) - p->data());

//     // Send packet with feedback
//     // input(0).push(p);

//     // Reset parameters for next feedback
//     //, memset(&_rlc_feedback, 0, sizeof(srv6_fec2_feedback));

// }

CLICK_ENDDECLS
EXPORT_ELEMENT(IP6SRv6FECDecode)
ELEMENT_MT_SAFE(IP6SRv6FECDecode)

