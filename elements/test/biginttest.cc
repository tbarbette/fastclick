// -*- c-basic-offset: 4 -*-
/*
 * biginttest.{cc,hh} -- regression test element for Bigint
 * Eddie Kohler
 *
 * Copyright (c) 2008 Meraki, Inc.
 * Copyright (c) 2011 Regents of the University of California
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
#include "biginttest.hh"
#include <click/hashtable.hh>
#include <click/error.hh>
#include <click/bigint.hh>
#if CLICK_USERLEVEL
# include <sys/time.h>
# include <sys/resource.h>
# include <unistd.h>
#endif
CLICK_DECLS

BigintTest::BigintTest()
{
}

#define CHECK(x, a, b) if (!(x)) return errh->error("%s:%d: test `%s' failed [%llu, %llu]", __FILE__, __LINE__, #x, a, b);
#define CHECKL(x, a, b) if (!(x)) return errh->error("%s:%d: test `%s' failed [%s, %llu]", __FILE__, __LINE__, #x, bigint::unparse_clear(a, 2).c_str(), b);
#define CHECK0(x) if (!(x)) return errh->error("%s:%d: test `%s' failed", __FILE__, __LINE__, #x);

static bool test_multiply(uint32_t a, uint32_t b, ErrorHandler *errh) {
    uint32_t x[2];
    Bigint<uint32_t>::multiply(x[1], x[0], a, b);
    uint64_t c = (((uint64_t) x[1]) << 32) | x[0];
    if (c != (uint64_t) a * b) {
        errh->error("%u * %u == %llu, not %llu", a, b, (uint64_t) a * b, c);
        return false;
    }
    return true;
}

static bool test_mul(uint64_t a, uint32_t b, ErrorHandler *errh) {
    uint32_t ax[2];
    ax[0] = a;
    ax[1] = a >> 32;
    uint32_t cx[2];
    cx[0] = cx[1] = 0;
    Bigint<uint32_t>::multiply_add(cx, ax, 2, b);
    uint64_t c = (((uint64_t) cx[1]) << 32) | cx[0];
    if (c != a * b) {
        errh->error("%llu * %u == %llu, not %llu", a, b, a * b, c);
        return false;
    }
    return true;
}

static bool test_div(uint64_t a, uint32_t b, ErrorHandler *errh) {
    assert(b);
    uint32_t ax[4];
    ax[0] = a;
    ax[1] = a >> 32;
    uint32_t r = Bigint<uint32_t>::divide(ax+2, ax, 2, b);
    uint64_t c = ((uint64_t) ax[3] << 32) | ax[2];
    if (c != a / b) {
        errh->error("%llu / %u == %llu, not %llu", a, b, a * b, c);
        return false;
    }
    if (r != a % b) {
        errh->error("%llu %% %u == %llu, not %u", a, b, a % b, r);
        return false;
    }
    return true;
}

static bool test_inverse(uint32_t a, ErrorHandler *errh) {
    assert(a & (1 << 31));
    uint32_t a_inverse = Bigint<uint32_t>::inverse(a);
    // "Inverse is floor((b * (b - a) - 1) / a), where b = 2^32."
    uint64_t b = (uint64_t) 1 << 32;
    uint64_t want_inverse = (b * (b - a) - 1) / a;
    assert(want_inverse < b);
    if (a_inverse != want_inverse) {
        errh->error("inverse(%u) == %u, not %u", a, (uint32_t) want_inverse, a_inverse);
        return false;
    }
    return true;
}

static bool test_add(uint64_t a, uint64_t b, ErrorHandler *errh) {
    uint32_t ax[6];
    ax[2] = a;
    ax[3] = a >> 32;
    ax[4] = b;
    ax[5] = b >> 32;
    Bigint<uint32_t>::add(ax[1], ax[0], ax[3], ax[2], ax[5], ax[4]);
    uint64_t c = ((uint64_t) ax[1] << 32) | ax[0];
    if (c != a + b) {
        errh->error("%llu + %llu == %llu, not %llu", a, b, a + b, c);
        return false;
    }
    return true;
}

static bool test_multiply64(uint64_t a, uint64_t b, ErrorHandler *errh) {
    uint64_t x[2], y[2];
    bigint::multiply(x[1], x[0], a, b);
    int_multiply(a, b, y[0], y[1]);
    if (memcmp(x, y, sizeof(x))) {
        errh->error("%u * %u == %s, not %s", a, b, bigint::unparse_clear(y, 2).c_str(),
            bigint::unparse_clear(x, 2).c_str());
        return false;
    }
    return true;
}

static bool test_mul64(uint64_t a[2], uint64_t b, ErrorHandler *errh) {
    uint64_t c[2] = {0, 0}, d[2], e[2];
    bigint::multiply_add(c, a, 2, b);
    int_multiply(a[0], b, d[0], d[1]);
    int_multiply(a[1], b, e[0], e[1]);
    d[1] += e[0];
    if (memcmp(c, d, sizeof(c))) {
        uint64_t tmp[2] = {a[0], a[1]};
        errh->error("%s * %llu == %s, not %s", bigint::unparse_clear(tmp, 2).c_str(), b,
            bigint::unparse_clear(d, 2).c_str(), bigint::unparse_clear(c, 2).c_str());
        return false;
    }
    return true;
}

static bool test_div64(uint64_t a[2], uint64_t b, ErrorHandler *errh) {
    assert(b);
    uint64_t c[2], q[2] = {0, 0}, rem;
    uint64_t r = bigint::divide(c, a, 2, b);
    // Upper 64 bits of the quotient
    q[1] = a[1] / b;
    rem = a[1] % b;
    // Lower 64 bits of the quotient
#ifdef __x86_64__
    __asm__("div %4" : "=d"(rem), "=a"(q[0]) : "d"(rem), "a"(a[0]), "rm"(b));
#else
    if (rem) {
        unsigned ashift = 0;
        unsigned bshift = ffs_msb(b) - 1;
        b <<= bshift;
        // While remainder >= 2^64,
        // subtract the divisor from the top bits of the remainder and accumulate quotient
        while (ashift < 64) {
            int s = ffs_msb(rem) - 1;
            if (s) {
                if (!rem || (unsigned) s >= 64 - ashift)
                    s = 64 - ashift;
                q[0] <<= s;
                rem = (rem << s) + ((a[0] << ashift) >> (64 - s));
                ashift += s;
                if (ashift == 64)
                    break;
            }
            if (rem < b) {
                q[0] <<= 1;
                ashift++;
                rem = (rem << 1) + (bool) (a[0] & (1UL << (64 - ashift)));
            }
            rem -= b;
            q[0]++;
        }
        q[0] <<= bshift;
        b >>= bshift;
    } else {
        rem = a[0];
    }
    // Remainder is now < 2^64, compute result directly
    q[0] += rem / b;
    rem = rem % b;
#endif
    if (memcmp(c, q, sizeof(c))) {
        uint64_t tmp[2] = {a[0], a[1]};
        errh->error("%s / %llu == %s, not %s", bigint::unparse_clear(tmp, 2).c_str(), b,
            bigint::unparse_clear(q, 2).c_str(), bigint::unparse_clear(c, 2).c_str());
        return false;
    }
    if (r != rem) {
        errh->error("%s %% %llu == %llu, not %llu", bigint::unparse_clear(a, 2).c_str(), b, rem, r);
        return false;
    }
    return true;
}

static bool test_inverse64(uint64_t a, ErrorHandler *errh) {
    assert(a & (1UL << 63));
    uint64_t a_inverse = bigint::inverse(a);
    // "Inverse is floor((b * (b - a) - 1) / a), where b = 2^64."
    uint64_t want_inverse[2] = {(uint64_t) -1, (uint64_t) -1};  // initialized to -1
    uint64_t c[2] = {0, -a};                                    // initialized to 2^64 * (2^64 - a)
    bigint::add(want_inverse[1], want_inverse[0], want_inverse[1], want_inverse[0], c[1], c[0]);
    bigint::divide(want_inverse, want_inverse, 2, a);
    assert(want_inverse[1] == 0);
    if (a_inverse != want_inverse[0]) {
        errh->error("inverse(%llu) == %llu, not %llu", a, want_inverse[0], a_inverse);
        return false;
    }
    return true;
}

static bool test_add64(uint64_t a[2], uint64_t b[2], ErrorHandler *errh) {
    uint64_t res[2];
    bigint::add(res[1], res[0], a[1], a[0], b[1], b[0]);

    uint64_t c[2] = {a[0] + b[0], a[1] + b[1]};
    if (a[0] > -b[0])
        c[1]++;
    if (memcmp(res, c, sizeof(res))) {
        uint64_t tmp[4] = {a[0], a[1], b[0], b[1]};
        errh->error("%s + %s == %s, not %s", bigint::unparse_clear(tmp, 2).c_str(),
            bigint::unparse_clear(&tmp[2], 2).c_str(), bigint::unparse_clear(c, 2).c_str(),
            bigint::unparse_clear(res, 2).c_str());
        return false;
    }
    return true;
}

int
BigintTest::initialize(ErrorHandler *errh)
{
    for (int i = 0; i < 3000; i++) {
        uint64_t a[2], b;
        a[0] = click_random() | ((uint64_t) click_random() << 31) | ((uint64_t) click_random() << 62);
        a[1] = click_random() | ((uint64_t) click_random() << 31) | ((uint64_t) click_random() << 62);
        b = click_random() | ((uint64_t) click_random() << 31) | ((uint64_t) click_random() << 62);
        CHECK(test_multiply(a[0], b, errh), (uint32_t) a[0], (uint32_t) b);
        CHECK(test_multiply64(a[0], b, errh), a[0], b);
        CHECK(test_mul(a[0], b, errh), (uint32_t) a[0], (uint32_t) b);
        CHECKL(test_mul64(a, b, errh), a, b);
    }
    for (int i = 0; i < 8000; i++) {
        uint64_t a = click_random() | ((uint64_t) click_random() << 31) | ((uint64_t) click_random() << 62);
        CHECK0(test_inverse(a | (1UL << 31), errh));
        CHECK0(test_inverse64(a | (1UL << 63), errh));
    }
    CHECK0(test_inverse(0x80000000, errh));
    for (int i = 0; i < 8000; i++) {
        uint64_t a[2], b[2];
        a[0] = click_random() | ((uint64_t) click_random() << 31) | ((uint64_t) click_random() << 62);
        a[1] = click_random() | ((uint64_t) click_random() << 31) | ((uint64_t) click_random() << 62);
        b[0] = click_random() | ((uint64_t) click_random() << 31) | ((uint64_t) click_random() << 62);
        b[1] = click_random() | ((uint64_t) click_random() << 31) | ((uint64_t) click_random() << 62);
        CHECK0(test_add(a[0], b[0], errh));
        CHECK0(test_add64(a, b, errh));
    }
    CHECK0(test_div(12884758640815563913ULL, 2506284098U, errh));
    for (int i = 0; i < 3000; i++) {
        uint64_t a[2], b;
        a[0] = click_random() | ((uint64_t) click_random() << 31) | ((uint64_t) click_random() << 62);
        a[1] = click_random() | ((uint64_t) click_random() << 31) | ((uint64_t) click_random() << 62);
        b = click_random() | ((uint64_t) click_random() << 31) | ((uint64_t) click_random() << 62);
        CHECK(test_div(a[0], b | (1UL << 31), errh), (uint32_t) a[0], (uint32_t) b | (1UL << 31));
        CHECKL(test_div64(a, b | (1UL << 63), errh), a, b | (1UL << 63));
    }
    for (int i = 0; i < 3000; i++) {
        uint64_t a[2], b;
        a[0] = click_random() | ((uint64_t) click_random() << 31) | ((uint64_t) click_random() << 62);
        a[1] = click_random() | ((uint64_t) click_random() << 31) | ((uint64_t) click_random() << 62);
        do
            b = click_random() | ((uint64_t) click_random() << 31) | ((uint64_t) click_random() << 62);
        while (!b);
        CHECK(test_div(a[0], b & ~(1UL << 31), errh), (uint32_t) a[0], (uint32_t) b & ~(1UL << 31));
        CHECKL(test_div64(a, b & ~(1UL << 63), errh), a, b & ~(1UL << 63));
        CHECK(test_div(a[0], b | (1UL << 31), errh), (uint32_t) a[0], (uint32_t) b | (1UL << 31));
        CHECKL(test_div64(a, b | (1UL << 63), errh), a, b | (1UL << 63));
    }

    uint32_t x[3] = { 3481, 592182, 3024921038U };
    CHECK0(Bigint<uint32_t>::unparse_clear(x, 3) == "55799944231168388787108580761");

    x[0] = 10;
    x[1] = 0;
    CHECK0(Bigint<uint32_t>::multiply(x, x, 2, 10) == 0 && x[0] == 100 && x[1] == 0);
    CHECK0(Bigint<uint32_t>::multiply(x, x, 2, 4191384139U) == 0 && x[0] == 0x9698A54CU && x[1] == 0x61U);

    {
        int32_t quot, rem;
        rem = int_remainder((int32_t) 0x80000000, 2, quot);
        CHECK0(quot == -0x40000000 && rem == 0);
        rem = int_remainder((int32_t) 0x80000000, 3, quot);
        CHECK0(quot == -715827883 && rem == 1);
    }

    errh->message("All tests pass!");
    return 0;
}

EXPORT_ELEMENT(BigintTest)
ELEMENT_REQUIRES(userlevel int64)
CLICK_ENDDECLS
