// -*- c-basic-offset: 4 -*-
/*
 * devirtualizetest.{cc,hh} -- regression test element for Vector
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
#include <click/error.hh>
#include "devirtualizetest.hh"
#include "../tools/click-devirtualize/cxxclass.hh"
#include "../tools/click-devirtualize/cxxclass.cc"


CLICK_DECLS

DevirtualizeTest::DevirtualizeTest()
{
}

#define CHECK(x) if (!(x)) return errh->error("%s:%d: test %<%s%> failed", __FILE__, __LINE__, #x);


CxxFunction DevirtualizeTest::makeFn(String code) {
    CxxInfo info;
    info.parse_file("class MyClass { int _bob; void fn(int arg) { "+code+" } };", true);
    auto cl = info._classes[0];
    auto fn = cl->_functions[0];
    return fn;
}
int
DevirtualizeTest::initialize(ErrorHandler *errh)
{
    CxxInfo info;
    info.parse_file("class MyClass { int _bob; void fn(int arg) { int a = _bob + 30; } };", true);
    CHECK(info._classes.size() == 1);
    auto cl = info._classes[0];
    CHECK(cl->name() == "MyClass");
    CHECK(cl->_functions.size() == 1);
    auto fn = cl->_functions[0];
    CHECK(!fn.replace_expr("a","1", true, true));
    CHECK(!fn.replace_expr("b","1", true, true));
    CHECK(!fn.replace_expr("_b","1", true, true));
    CHECK(fn.replace_expr("_bob","1", true, true));
    CHECK(fn.replace_expr("1","_bob", true, true));
    CHECK(!fn.replace_expr("bob","1", true, true));
    CHECK(fn.replace_expr("_bob","bab", true, true));
    if (fn.body().trim() != "int a = bab + 30;")
        click_chatter("%s", fn.body().trim().c_str());
    CHECK(fn.body().trim() == "int a = bab + 30;");

    
    fn = makeFn("call(a!TEMPVAL!)");

    CHECK(fn.replace_expr("!TEMPVAL!",", 7", false, true));

    errh->message("All tests pass!");
    return 0;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(DevirtualizeTest)
