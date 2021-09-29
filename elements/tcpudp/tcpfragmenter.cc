/*
 * tcpipencap.{cc,hh} -- element encapsulates packet in TCP/IP header
 * Cliff Frey
 *
 * Copyright (c) 2010 Meraki, Inc.
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
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include "tcpfragmenter.hh"
#include <click/args.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/standard/alignmentinfo.hh>
CLICK_DECLS

TCPFragmenter::TCPFragmenter()
    : _mtu(0), _mtu_anno(-1)
{
    _fragments = 0;
    _fragmented_count = 0;
    _count = 0;

    #if HAVE_BATCH
        in_batch_mode = BATCH_MODE_YES;
    #endif
}

TCPFragmenter::~TCPFragmenter()
{
}

int
TCPFragmenter::configure(Vector<String> &conf, ErrorHandler *errh)
{
    uint16_t mtu;
    int mtu_anno = -1;

    if (Args(conf, this, errh)
	.read_or_set_p("MTU", mtu, 1500)
	.read("MTU_ANNO", AnnoArg(2), mtu_anno)
	.read_or_set("SW_CHECKSUM", _sw_checksum, true)
	.complete() < 0)
	return -1;

    if (mtu == 0 && mtu_anno == -1)
	return errh->error("At least one of MTU and MTU_ANNO must be set");

    _mtu = mtu;
    _mtu_anno = mtu_anno;
    return 0;
}

void
TCPFragmenter::push(int, Packet *p)
{
    int mtu = _mtu;
    if (_mtu_anno >= 0 && p->anno_u16(_mtu_anno) &&
	(!mtu || mtu > p->anno_u16(_mtu_anno)))
	mtu = p->anno_u16(_mtu_anno);

    int32_t hlen;
    int32_t tcp_len;
    {
        const click_ip *ip = p->ip_header();
        const click_tcp *tcp = p->tcp_header();
        hlen = (ip->ip_hl<<2) + (tcp->th_off<<2);
        tcp_len = ntohs(ip->ip_len) - hlen;
    }

    int max_tcp_len = mtu - hlen;

    _count++;
    if (!mtu || max_tcp_len <= 0 || tcp_len < max_tcp_len) {
        output(0).push(p);
        return;
    }

    _fragmented_count++;
    for (int offset = 0; offset < tcp_len; offset += max_tcp_len) {
        Packet *p_clone;
        if (offset + max_tcp_len < tcp_len)
            p_clone = p->clone();
        else {
            p_clone = p;
            p = 0;
        }
        if (!p_clone)
            break;
        WritablePacket *q = p_clone->uniqueify();

        p_clone = 0;
        click_ip *ip = q->ip_header();
        click_tcp *tcp = q->tcp_header();
        uint8_t *tcp_data = ((uint8_t *)tcp) + (tcp->th_off<<2);
        int this_len = tcp_len - offset > max_tcp_len ? max_tcp_len : tcp_len - offset;
        if (offset != 0)
            memcpy(tcp_data, tcp_data + offset, this_len);
        q->take(tcp_len - this_len);
        ip->ip_len = htons(q->end_data() - q->network_header());
        ip->ip_sum = 0;
#if HAVE_FAST_CHECKSUM
        ip->ip_sum = ip_fast_csum((unsigned char *)ip, q->network_header_length() >> 2);
#else
        ip->ip_sum = click_in_cksum((unsigned char *)ip, q->network_header_length());
#endif

        if ((tcp->th_flags & TH_FIN) && offset + mtu < tcp_len)
            tcp->th_flags ^= TH_FIN;

        tcp->th_seq = htonl(ntohl(tcp->th_seq) + offset);
        tcp->th_sum = 0;

        // now calculate tcp header cksum
        int plen = q->end_data() - (uint8_t*)tcp;
        unsigned csum = click_in_cksum((unsigned char *)tcp, plen);
        tcp->th_sum = click_in_cksum_pseudohdr(csum, ip, plen);
        _fragments++;
        output(0).push(q);
    }
}


/*
 * Copy original[offset, offset + len] to q[0, len], fixing header, and only if needed (if offset == 0)).
 */
WritablePacket* TCPFragmenter::split_packet(WritablePacket* original, int offset, int len, const int &tcp_payload, bool last) {
            WritablePacket *q;
            if (offset != 0) {
                q = WritablePacket::make_similar(original, len + tcp_payload);
                if (!q)
                    return 0;
                memcpy(q->data(), original->data(), tcp_payload);
                q->copy_annotations(original);
                q->set_mac_header(original->mac_header() ? q->data() + original->mac_header_offset() : 0);
                q->set_network_header(original->network_header() ? q->data() + original->network_header_offset() : 0);
                if (original->has_transport_header())
                    q->set_transport_header(q->data() + original->transport_header_offset());
            } else {
                q = original;
            }
            click_ip *ip = q->ip_header();
            click_tcp *tcp = q->tcp_header();
            if (offset != 0) {
                memcpy(q->data() + tcp_payload, original->data() + tcp_payload + offset, len);
            }

            q->take(q->length() - len -tcp_payload);
            ip->ip_len = htons(q->end_data() - q->network_header());

            if ((tcp->th_flags & TH_FIN) && !last)
                tcp->th_flags ^= TH_FIN;

            tcp->th_seq = htonl(ntohl(tcp->th_seq) + offset);

            if (!_sw_checksum)
                resetTCPChecksum(q);
            else
                computeTCPChecksum(q);

            return q;
}


#if HAVE_BATCH
void
TCPFragmenter::push_batch(int, PacketBatch *batch)
{
    // We create a new batch because the TCP fragmentation may result in the creation of
    // new packets in the middle of the batch
    PacketBatch* newBatch = NULL;

    // We process each packet of the old batch
    FOR_EACH_PACKET_SAFE(batch, p)
    {
        int mtu = _mtu;
        if (_mtu_anno >= 0 && p->anno_u16(_mtu_anno) &&
	(!mtu || mtu > p->anno_u16(_mtu_anno)))
	mtu = p->anno_u16(_mtu_anno);

        int32_t hlen;
        int32_t tcp_len;
        {
            const click_ip *ip = p->ip_header();
            const click_tcp *tcp = p->tcp_header();
            hlen = (ip->ip_hl<<2) + (tcp->th_off<<2);
            tcp_len = ntohs(ip->ip_len) - hlen;
        }

        int max_tcp_len = mtu - hlen;

        _count++;
        if (!mtu || max_tcp_len <= 0 || tcp_len <= max_tcp_len) {
            // We add the packet to the batch we are building instead of sending it immediately
            if(newBatch == NULL)
                newBatch = PacketBatch::make_from_packet(p);
            else
                newBatch->append_packet(p);
            // Process the next packet
            continue;
        }

        _fragmented_count++;


        WritablePacket *original = p->uniqueify();

        click_tcp *tcp = original->tcp_header();
        int tcp_payload = original->transport_header_offset() + (tcp->th_off<<2);
        Packet* last = original;
        int frag_count = 1;
        for (int offset = max_tcp_len; offset < tcp_len; offset += max_tcp_len) {
            int length = tcp_len;
            if (length + offset > tcp_len)
                length = tcp_len - offset;
            WritablePacket* q  = split_packet(original, offset, length, tcp_payload, offset + max_tcp_len >= tcp_len);
            if (q == 0) {
                click_chatter("OOM");
                abort();
            }
            frag_count++;
            last->set_next(q);
            last = q;
        }
        _fragments+= frag_count;
        split_packet(original, 0, max_tcp_len , tcp_payload, false);
        if(newBatch == NULL)
            newBatch = PacketBatch::make_from_simple_list(original, last, frag_count);
        else
            newBatch->append_simple_list(original, last, frag_count);
    }

    // We now send the batch we built
    if(newBatch != NULL)
        output_push_batch(0, newBatch);
}
#endif

void
TCPFragmenter::add_handlers()
{
    add_data_handlers("fragments", Handler::OP_READ, &_fragments);
    add_data_handlers("fragmented_count", Handler::OP_READ, &_fragmented_count);
    add_data_handlers("count", Handler::OP_READ, &_count);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(TCPFragmenter)
ELEMENT_MT_SAFE(TCPFragmenter)
