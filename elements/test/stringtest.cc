// -*- c-basic-offset: 4 -*-
/*
 * bitvectortest.{cc,hh} -- regression test element for Bitvector
 * Eddie Kohler
 *
 * Copyright (c) 2012 Eddie Kohler
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
#include "stringtest.hh"
#include <click/string.hh>
#include <click/error.hh>
#include <click/vector.hh>
CLICK_DECLS

StringTest::StringTest()
{
}

#define CHECK(x) if (!(x)) return errh->error("%s:%d: test %<%s%> failed", __FILE__, __LINE__, #x);

int
StringTest::initialize(ErrorHandler *errh)
{
    String s;
    CHECK(s.length() == 0);

    s = "a simple string";
    Vector<String> v = s.split(' ');
    CHECK(v.size() == 3);
    CHECK(v[0] == "a");
    CHECK(v[1] == "simple");
    CHECK(v[2] == "string");

    errh->message("All tests pass!");
    return 0;
}

EXPORT_ELEMENT(StringTest)
CLICK_ENDDECLS
