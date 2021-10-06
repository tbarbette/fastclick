#include <click/config.h>
#include "atomic.hh"
#include <click/error.hh>
CLICK_DECLS

AtomicTest::AtomicTest()
{
}

static void test_32(ErrorHandler *)
{
    click_chatter("atomic_uint32_t is using %s implementation", atomic_uint32_t::use_builtins() ? "builtins" : "click" );

    atomic_uint32_t a;
    a = 0;
    click_chatter("[01] a=%u -> %s",  (uint32_t)a, a==0 ? "PASS" : "FAIL");
    a++;
    click_chatter("[02] a=%u -> %s",  (uint32_t)a, a==1 ? "PASS" : "FAIL");
    a+=1;
    click_chatter("[03] a=%u -> %s",  (uint32_t)a, a==2 ? "PASS" : "FAIL");
    a-=1;
    click_chatter("[04] a=%u -> %s",  (uint32_t)a, a==1 ? "PASS" : "FAIL");
    uint32_t u = a.swap(5);
    click_chatter("[05] a=%u, u=%i -> %s", (uint32_t)a, u,  a==5 && u==1 ? "PASS" : "FAIL");
    u = a.fetch_and_add(10);
    click_chatter("[06] a=%u, u=%i -> %s", (uint32_t)a, u,  a==15 && u==5 ? "PASS" : "FAIL");
    u = a.dec_and_test();
    click_chatter("[07] a=%u, u=%i -> %s", (uint32_t)a, u,  a==14 && u==0 ? "PASS" : "FAIL");
    a=1;
    u = a.dec_and_test();
    click_chatter("[08] a=%u, u=%i -> %s", (uint32_t)a, u,  a==0 && u==1 ? "PASS" : "FAIL");
    a=2;
    u = a.nonatomic_dec_and_test();
    click_chatter("[09] a=%u, u=%i -> %s", (uint32_t)a, u,  a==1 && u==0 ? "PASS" : "FAIL");
    a=1;
    u = a.nonatomic_dec_and_test();
    click_chatter("[10] a=%u, u=%i -> %s", (uint32_t)a, u,  a==0 && u==1 ? "PASS" : "FAIL");
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    u = a.compare_and_swap(0,2);
    click_chatter("[11] a=%u, u=%i -> %s", (uint32_t)a, u,  a==2 && u==1 ? "PASS" : "FAIL");
    u = a.compare_and_swap(0,5);
    click_chatter("[12] a=%u, u=%i -> %s", (uint32_t)a, u,  a==2 && u==0 ? "PASS" : "FAIL");
#pragma GCC diagnostic pop
    uint32_t b =5;
    u = atomic_uint32_t::swap(b,7);
    click_chatter("[13] b=%u, u=%i -> %s", (uint32_t)b, u,  b==7 && u==5 ? "PASS" : "FAIL");
    atomic_uint32_t::inc(b);
    click_chatter("[14] b=%u -> %s", (uint32_t)b,  b==8 ? "PASS" : "FAIL");
    u=atomic_uint32_t::dec_and_test(b);
    click_chatter("[15] b=%u, u=%i -> %s", (uint32_t)b, u,  b==7 && u==0 ? "PASS" : "FAIL");
    b=1;
    u=atomic_uint32_t::dec_and_test(b);
    click_chatter("[16] b=%u, u=%i -> %s", (uint32_t)b, u,  b==0 && u==1 ? "PASS" : "FAIL");

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

    b=5;
    u = atomic_uint32_t::compare_and_swap(b,0,2);
    click_chatter("[17] b=%i, u=%u -> %s", b,  (uint32_t)u,  b==5 && u==0 ? "PASS" : "FAIL");
    u = atomic_uint32_t::compare_and_swap(b,5,2);
    click_chatter("[18] b=%i, u=%u -> %s", b,  (uint32_t)u,  b==2 && u==1 ? "PASS" : "FAIL");
#pragma GCC diagnostic pop

#if CLICK_ATOMIC_BUILTINS
    click_chatter("[--] Not testing compare_swap!");
#else
    a=11;
    u = a.compare_swap(0,2);
    click_chatter("[19] a=%u, u=%u -> %s", (uint32_t)a, u,  a==11 && u==11 ? "PASS" : "FAIL");
    u = a.compare_swap(11,5);
    click_chatter("[20] a=%u, u=%u -> %s", (uint32_t)a, u,  a==5 && u==11 ? "PASS" : "FAIL");
    b=5;
    u = atomic_uint32_t::compare_swap(b,0,2);
    click_chatter("[21] b=%u, u=%u -> %s", b, u,  b==5 && u==5 ? "PASS" : "FAIL");
    u = atomic_uint32_t::compare_swap(b,5,2);
    click_chatter("[22] b=%u, u=%u -> %s", b, u,  b==2 && u==5 ? "PASS" : "FAIL");
#endif
}


static void test_64(ErrorHandler *)
{
    click_chatter("atomic_uint64_t is using %s implementation", atomic_uint32_t::use_builtins() ? "builtins" : "click" );

    atomic_uint64_t a;
    a = 0;
    click_chatter("[01] a=%i -> %s", a, a==0 ? "PASS" : "FAIL");
    a++;
    click_chatter("[02] a=%i -> %s", a, a==1 ? "PASS" : "FAIL");
    a+=1;
    click_chatter("[03] a=%i -> %s", a, a==2 ? "PASS" : "FAIL");
    a-=1;
    click_chatter("[04] a=%i -> %s", a, a==1 ? "PASS" : "FAIL");
    uint32_t u = a.fetch_and_add(10);
    click_chatter("[05] a=%i, u=%i -> %s", a, u,  a==11 && u==1 ? "PASS" : "FAIL");

#if CLICK_ATOMIC_BUILTINS
    click_chatter("[--] Not testing compare_swap!");
#else
    u = a.compare_swap(0,2);
    click_chatter("[06] a=%i, u=%i -> %s", a, u,  a==11 && u==11 ? "PASS" : "FAIL");
    u = a.compare_swap(11,5);
    click_chatter("[07] a=%i, u=%i -> %s", a, u,  a==5 && u==11 ? "PASS" : "FAIL");
    uint64_t b =5;

    u = atomic_uint64_t::compare_swap(b,0,2);
    click_chatter("[08] b=%i, u=%i -> %s", b, u,  b==5 && u==5 ? "PASS" : "FAIL");
    u = atomic_uint64_t::compare_swap(b,5,2);
    click_chatter("[09] b=%i, u=%i -> %s", b, u,  b==2 && u==5 ? "PASS" : "FAIL");
#endif
}




int
AtomicTest::initialize(ErrorHandler *errh)
{

    test_32(errh);
    test_64(errh);

    errh->message("Test finished");
    exit(0);
    return 0;
}

EXPORT_ELEMENT(AtomicTest)
CLICK_ENDDECLS
