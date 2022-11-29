// -*- c-basic-offset: 4 -*-
/*
 * functiontest.{cc,hh} -- regression test element for other Click functions
 * Eddie Kohler
 *
 * Copyright (c) 2008 Regents of the University of California
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
#include "functiontest.hh"
#include <click/confparse.hh>
#include <click/integers.hh>
#if CLICK_USERLEVEL
# include <click/userutils.hh>
#endif
#include <click/error.hh>
#include <click/tinyexpr.hh>
CLICK_DECLS

FunctionTest::FunctionTest()
{
}

#define CHECK(x) if (!(x)) return errh->error("%s:%d: test %<%s%> failed", __FILE__, __LINE__, #x);

int
FunctionTest::initialize(ErrorHandler *errh)
{
    CHECK((same_type<int, int>::value));
    CHECK((!same_type<int, long>::value));
    CHECK((!same_type<int, const int>::value));
    CHECK((!same_type<int, int*>::value));
    CHECK((types_compatible<int, int>::value));
    CHECK((!types_compatible<int, long>::value));
    CHECK((types_compatible<int, const int>::value));
    CHECK((!types_compatible<int, int*>::value));

    CHECK(ffs_msb(0U) == 0);
    CHECK(ffs_msb(1U) == 32);
    CHECK(ffs_msb(0x80000000U) == 1);
    CHECK(ffs_msb(0x00010000U) == 16);

    CHECK(ffs_lsb(0U) == 0);
    CHECK(ffs_lsb(1U) == 1);
    CHECK(ffs_lsb(0x80000000U) == 32);
    CHECK(ffs_lsb(0x00010000U) == 17);

    CHECK(next_pow2(31U) == 32);
    CHECK(next_pow2(32U) == 32);

    CHECK(next_msb(0U) == 1);
    CHECK(next_msb(1U) == 1);
    CHECK(next_msb(32U) == 5);
    CHECK(next_msb(255U) == 8);
    CHECK(next_msb(256U) == 8);

    CHECK(int_sqrt(0U) == 0);
    CHECK(int_sqrt(1U) == 1);
    CHECK(int_sqrt(3U) == 1);
    CHECK(int_sqrt(4U) == 2);
    CHECK(int_sqrt(5U) == 2);
    CHECK(int_sqrt(120U) == 10);
    CHECK(int_sqrt(121U) == 11);
    CHECK(int_sqrt(0x40000000U) == 0x8000);
    CHECK(int_sqrt(0xFFFFFFFEU) == 0xFFFF);
    CHECK(int_sqrt(0xFFFFFFFFU) == 0xFFFF);

#if HAVE_INT64_TYPES && HAVE_INT64_DIVIDE
    CHECK(int_sqrt((uint64_t) 0xFFFFFFFFU) == 0xFFFF);
    CHECK(int_sqrt((uint64_t) 1 << 32) == 0x10000);
    CHECK(int_sqrt(~((uint64_t) 0)) == 0xFFFFFFFFU);
#endif

#if CLICK_USERLEVEL
    CHECK(glob_match("", "*"));
    CHECK(glob_match("a", "\\a"));
    CHECK(glob_match("*", "\\*"));
    CHECK(!glob_match("a", "\\*"));
    CHECK(glob_match("Q", "*"));
    CHECK(glob_match("QX", "*"));
    CHECK(glob_match("Q", "Q*"));
    CHECK(glob_match("QX", "Q*"));
    CHECK(!glob_match("Q.x", "Q*.o"));
    CHECK(glob_match("QXajdsifds.o", "Q*.o"));
    CHECK(glob_match("x.o", "?.o"));
    CHECK(!glob_match("x.c", "?.o"));
    CHECK(!glob_match("xx.o", "?.o"));
    CHECK(glob_match("x.o.d", "x*.?*.*"));
    CHECK(!glob_match("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaacba", "*aa*aa*aa*aa*aa*aa*aa*aa*aa*aa*aa*aa*aa*b*c*"));
#endif

    String expr_s = "((sin(-pi/2 + (x/10)^2.5) * (-x/45 + 1) + 1) * ((200 - 1) / 2) + 1)";

    TinyExpr expr = TinyExpr::compile(expr_s, 1);
    CHECK(expr.eval(0) == 1);
    CHECK(abs(expr.eval(5)  - 13.4339) < 0.01);
    CHECK(abs(expr.eval(45)  - 100.5) < 0.01);

    CHECK(TinyExpr::compile("squarewave(0.0)",0).eval() == 1);
    CHECK(TinyExpr::compile("squarewave(0.49)",0).eval() == 1);
    CHECK(TinyExpr::compile("squarewave(0.5)",0).eval() == -1);
    CHECK(TinyExpr::compile("squarewave(0.89)",0).eval() == -1);
    CHECK(TinyExpr::compile("squarewave(1)",0).eval() == 1);
    CHECK(TinyExpr::compile("squarewave(1.21)",0).eval() == 1);
    CHECK(TinyExpr::compile("squarewave(1.5)",0).eval() == -1);

    expr = TinyExpr::compile("(squarewave(((x + 20 / 2) * 1/20) ^ 2.5) * (-x / 45 + 1) + 1) * ((200 -1) / 2) + 1", 1);


    CHECK(TinyExpr::compile("min(1, 2)",0).eval() == 1);

    CHECK(abs(TinyExpr::compile("min(1, (0.1 + (x/10)))",1).eval(1) - 0.2) < 0.01);

    CHECK(abs(expr.eval(0) - 200) < 0.01);
    CHECK(abs(expr.eval(5)  - 3401.0/18) < 0.01);
    CHECK(abs(expr.eval(45)  - 201.0/2) < 0.01);
    errh->message("All tests pass!");
    return 0;
}

EXPORT_ELEMENT(FunctionTest)
CLICK_ENDDECLS
