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
CLICK_DECLS

ResetIPChecksum::ResetIPChecksum()
    : _drops(0)
{
}

ResetIPChecksum::~ResetIPChecksum()
{
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
            rte_mbuf* mbuf = (struct rte_mbuf *) p->destructor_argument();
            mbuf->l2_len = p->mac_header_length();
            mbuf->l3_len = p->network_header_length();
            //mbuf->l4_len = tcph->th_off << 2;
            mbuf->ol_flags |= PKT_TX_IP_CKSUM | PKT_TX_IPV4;
            //tcph->th_sum = rte_ipv4_phdr_cksum((struct ipv4_hdr *)iph, mbuf->ol_flags);
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
