/*
 * resetipchecksum.{cc,hh} -- element resets IP header checksum
 * Tom Barbette
 *
 * Copyright (c) 2019 Tom Barbette
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
#include "resetipchecksum.hh"
#include <click/glue.hh>
#include <click/dpdkdevice.hh>
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include <clicknet/udp.h>
#include <rte_ip.h>

#if RTE_VERSION >= RTE_VERSION_NUM(22,07,0,0)
#define PKT_TX_IP_CKSUM RTE_MBUF_F_TX_IP_CKSUM
#define PKT_TX_TCP_CKSUM RTE_MBUF_F_TX_TCP_CKSUM
#define PKT_TX_IPV4 RTE_MBUF_F_TX_IPV4
#define PKT_TX_UDP_CKSUM RTE_MBUF_F_TX_UDP_CKSUM
#endif

CLICK_DECLS

ResetIPChecksum::ResetIPChecksum()
    : _drops(0)
{
}

ResetIPChecksum::~ResetIPChecksum()
{
}

int
ResetIPChecksum::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
            .read_or_set_p("L4", _l4, false)
            .complete() < 0)
		return -1;

    return 0;
}

Packet *
ResetIPChecksum::simple_action(Packet *p_in)
{
    if (WritablePacket *p = p_in->uniqueify()) {
        click_ip *iph = p->ip_header();

        iph->ip_sum = 0;
        if (!DPDKDevice::is_dpdk_buffer(p))
        {
            click_chatter("Not a DPDK buffer. For max performance, arrange TCP element to always work on DPDK buffers");
        } else {
#if CLICK_PACKET_USE_DPDK
            rte_mbuf* mbuf = (struct rte_mbuf *) p;
#else
            rte_mbuf* mbuf = (struct rte_mbuf *) p->destructor_argument();
#endif
            mbuf->l2_len = p->mac_header_length();
            mbuf->l3_len = p->network_header_length();

            if (_l4) {
                uint16_t sum;
			if (iph->ip_p == IP_PROTO_TCP) {
                    click_tcp *tcph = p->tcp_header();
                    mbuf->l4_len = tcph->th_off << 2;
                #if RTE_VERSION >= RTE_VERSION_NUM(19,8,0,0)
                    sum = rte_ipv4_phdr_cksum((const struct rte_ipv4_hdr *)iph, mbuf->ol_flags);
                #else
                    sum = rte_ipv4_phdr_cksum((struct ipv4_hdr *)iph, mbuf->ol_flags);
                #endif
					tcph->th_sum = sum;
					mbuf->ol_flags |= PKT_TX_IP_CKSUM | PKT_TX_TCP_CKSUM | PKT_TX_IPV4;
				} else if (iph->ip_p == IP_PROTO_UDP) {
			        click_udp *udph = p->udp_header();
			        mbuf->l4_len = sizeof(click_udp);
                #if RTE_VERSION >= RTE_VERSION_NUM(19,8,0,0)
                    sum = rte_ipv4_phdr_cksum((const struct rte_ipv4_hdr *)iph, mbuf->ol_flags);
                #else
                    sum = rte_ipv4_phdr_cksum((struct ipv4_hdr *)iph, mbuf->ol_flags);
                #endif
			        udph->uh_sum = sum;
				mbuf->ol_flags |= PKT_TX_IP_CKSUM | PKT_TX_UDP_CKSUM | PKT_TX_IPV4;
			} else {
			mbuf->ol_flags |= PKT_TX_IP_CKSUM | PKT_TX_IPV4;
			}
            } else {
		mbuf->ol_flags |= PKT_TX_IP_CKSUM | PKT_TX_IPV4;
            }
        }
        return p;
    } else {
        if (++_drops == 1)
            click_chatter("ResetIPChecksum: bad input packet");
        p->kill();
    }
    return 0;
}

#if HAVE_BATCH
PacketBatch *
ResetIPChecksum::simple_action_batch(PacketBatch *batch)
{
    EXECUTE_FOR_EACH_PACKET_DROPPABLE(ResetIPChecksum::simple_action, batch, [](Packet *){});
    return batch;
}
#endif

void
ResetIPChecksum::add_handlers()
{
    add_data_handlers("drops", Handler::OP_READ, &_drops);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(dpdk)
EXPORT_ELEMENT(ResetIPChecksum)
ELEMENT_MT_SAFE(ResetIPChecksum)
