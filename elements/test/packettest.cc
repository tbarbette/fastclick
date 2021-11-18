// -*- c-basic-offset: 4 -*-
/*
 * packettest.{cc,hh} -- regression test element for packets
 * Eddie Kohler
 *
 * Copyright (c) 2002 International Computer Science Institute
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
#include "packettest.hh"
#include <click/error.hh>
#include <clicknet/ip.h>
#include <clicknet/ip6.h>
CLICK_DECLS

PacketTest::PacketTest()
{
}

#define CHECK(x) if (!(x)) return errh->error("%s:%d: test `%s' failed", __FILE__, __LINE__, #x);
#define CHECK_DATA(x, y, l) CHECK(memcmp((x), (y), (l)) == 0)
#define CHECK_ALIGNED(x) CHECK((reinterpret_cast<uintptr_t>((x)) & 3) == 0)

int
PacketTest::initialize(ErrorHandler *errh)
{
    const unsigned char *lowers = (const unsigned char *)"abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
    IPAddress addr(String("1.2.3.4"));

    Packet *p;
    WritablePacket *p1;
    Packet *p2;
    WritablePacket *p3;

#ifndef CLICK_NOINDIRECT
    p = Packet::make(10);
    Packet* pref1 = p->clone();
    CHECK(p->shared());
    CHECK(pref1->shared());
    p2 = p->uniqueify();
    CHECK(p2);
    CHECK(!p2->shared());
    CHECK(pref1->buffer() != p2->buffer());
    CHECK(p2->data() != pref1->data());
    p1 = Packet::make(10);
    CHECK(pref1 != p1 && pref1->buffer() != p1->buffer());
    CHECK(pref1 != p2 && pref1->buffer() != p1->buffer());
    CHECK(p1 != p2 && p1->buffer() != p2->buffer());
    p1->kill();
    p2->kill();
    p1 = Packet::make(10);
    p2 = Packet::make(10);
    CHECK(pref1 != p1 && pref1->buffer() != p1->buffer());
    CHECK(pref1 != p2 && pref1->buffer() != p2->buffer());
    pref1->kill();
    p1->kill();
    p2->kill();
#endif

    p = Packet::make(10, lowers, 20, 30);
    CHECK(p->headroom() >= 10);
    CHECK(p->tailroom() >= 30);
    CHECK(p->length() == 20);
    CHECK(p->buffer_length() >= 60);
    CHECK_DATA(p->data(), lowers, 20);
    CHECK(!p->mac_header());
    CHECK(!p->network_header());
    CHECK(!p->transport_header());
    p->set_mac_header(p->data(), 10);
    CHECK(p->network_header() == p->data() + 10);
    p->set_dst_ip_anno(addr);

    p1 = p->push(5);
    // p is dead
    CHECK(p == p1);
    CHECK(p1->headroom() >= 5);
    CHECK(p1->tailroom() >= 30);
    CHECK(p1->length() == 25);
    CHECK_DATA(p1->data() + 5, lowers, 20);
    CHECK(p1->mac_header() == p->data() + 5);
    CHECK(p1->network_header() == p->data() + 15);
    CHECK(p1->dst_ip_anno() == addr);

#ifndef CLICK_NOINDIRECT
    p2 = p1->clone();
    CHECK(p2 != p1);
    CHECK(p2->data() == p1->data());
    CHECK(p2->length() == 25);
    CHECK(p1->shared() && p2->shared());
    CHECK(p1->mac_header() == p2->mac_header());
    CHECK(p2->dst_ip_anno() == addr);

    p3 = p2->push(5);
    // p2 is dead
    CHECK(p3 != p1);
    CHECK(p3->length() == 30);
    CHECK_DATA(p3->data() + 10, lowers, 20);
    memcpy(p3->data(), lowers, 10);
    memcpy(p1->data(), lowers, 5);
    CHECK_DATA(p3->data(), lowers, 10);
    CHECK_DATA(p1->data(), lowers, 5);
    CHECK(p3->mac_header() != p1->mac_header());
    CHECK(p3->mac_header() == p3->data() + 10);
    CHECK(p3->network_header() == p3->data() + 20);
    CHECK(!p1->shared() && !p3->shared());
    CHECK(p3->dst_ip_anno() == addr);

    p1->kill();
    p3->kill();
#endif

#if 0
    // time cloning
    p = Packet::make(4);
    for (int i = 0; i < 40000000; i++)
	p->clone()->kill();
    p->kill();
#endif

    // test shift_data()
    p = Packet::make(10, lowers, 60, 4);
    int tail = p->tailroom();
    int head = p->headroom();
    const unsigned char* data = p->data();
    CHECK(p->headroom() == 10 && p->tailroom() >= 4);
    p = p->shift_data(-2);
    CHECK(p->data() == data - 2);
    CHECK(p->headroom() == head - 2 && p->tailroom() == tail + 2);
    CHECK(p->length() == 60);
    CHECK_DATA(p->data(), lowers, 60);
    CHECK_ALIGNED(p->data());
    p->kill();

    p = Packet::make(9, lowers, 60, 4);
    p = p->shift_data(3);
    CHECK(p->headroom() == 12 && p->tailroom() >= 1 && p->length() == 60);
    CHECK_DATA(p->data(), lowers, 60);
    CHECK_ALIGNED(p->data());
    p->kill();

    p = Packet::make(1, lowers, 60, 4);
    tail = p->tailroom();
    head = p->headroom();
    p = p->shift_data(-5);
    CHECK(p->tailroom() >= 9 && p->length() == 60);
    CHECK_DATA(p->data(), lowers, 60);
    CHECK_ALIGNED(p->data());
    p->kill();

    p = Packet::make(5, lowers, 60, 2);
    p = p->shift_data(3);
    CHECK(p->headroom() == 8 && p->length() == 60);
    CHECK_DATA(p->data(), lowers, 60);
    CHECK_ALIGNED(p->data());
    p->kill();

    p = Packet::make(5, lowers, 60, 2);
    p->set_mac_header(p->data(), 2);
    p->pull(2);
    p = p->shift_data(-3);
    CHECK(p->mac_header() == p->data() - 2);
    CHECK(p->headroom() >= 2 && p->length() == 58);
    CHECK_DATA(p->mac_header(), lowers, 2);
    CHECK_DATA(p->data(), lowers + 2, 58);
    CHECK_ALIGNED(p->data());
    p->kill();

    PacketBatch* batch = PacketBatch::make_from_packet(Packet::make(1, lowers, 60, 2));
    for (int i = 2; i <= 100; i++)
        batch->append_packet(Packet::make(i, lowers, 60, 2));
    int i = 0;
    auto fnt = [](Packet *p_in, std::function<void(Packet*)>add){
            int r = click_random() % 3;
            if (r == 0) {
                add(p_in->clone()->uniqueify());
                p_in->kill();
            } else if (r == 1) {
                Packet* c = p_in->clone();
                add(p_in->uniqueify());
                c->kill();
            } else {
                add(p_in);
            }
    };
    EXECUTE_FOR_EACH_PACKET_ADD(fnt, batch);
    CHECK(batch->count() == 100);
    CHECK(batch->first()->find_count() == 100);
    i = 1;
    FOR_EACH_PACKET(batch,p) {
        CHECK(p->headroom() == i++);
    }

    // Also check some packet header definition properties.
    union {
	click_ip ip4;
	click_ip6 ip6;
	uint32_t u32[10];
    } fakehdr;
    fakehdr.u32[0] = htonl(0x456789AB);
    CHECK(fakehdr.ip4.ip_v == 4);
    CHECK(fakehdr.ip4.ip_hl == 5);
    CHECK(fakehdr.ip4.ip_tos == 0x67);
    CHECK(fakehdr.ip4.ip_len == htons(0x89AB));
    CHECK(fakehdr.ip6.ip6_v == 4);
    CHECK(fakehdr.ip6.ip6_vfc == 0x45);
    CHECK(fakehdr.ip6.ip6_flow == htonl(0x456789AB));

    errh->message("All tests pass!");
    return 0;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(PacketTest)
