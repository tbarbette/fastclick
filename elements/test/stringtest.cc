// -*- c-basic-offset: 4 -*-
/*
 * stringtest.{cc,hh} -- regression test element for String
 * Tom Barbette and Georgios Katsikas
 *
 * Copyright (c) 2018-2019 KTH Royal Institute of Technology
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
#include <click/glue.hh>
#include <click/straccum.hh>
#include <click/error.hh>
CLICK_DECLS


StringTest::StringTest()
{
}

#define CHECK(x) if (!(x)) return errh->error("%s:%d: test `%s' failed", __FILE__, __LINE__, #x);

int
StringTest::initialize(ErrorHandler *errh)
{
    String empty = "";
    CHECK(empty == String::make_empty());

    // Split test
    CHECK(empty.split(';').size() == 0);
    CHECK(String("HELLO").split(';').size() == 1);
    CHECK(String("HELLO").split(';')[0] == "HELLO");
    CHECK(String("HELLO;YOU").split(';').size() == 2);
    CHECK(String("HELLO;YOU").split(';')[0] == "HELLO");
    CHECK(String("HELLO;YOU").split(';')[1] == "YOU");

    // Split with multiple delimiters
    CHECK(String("").split(";").size() == 0);
    CHECK(String("HELLO;YOU").split("").size() == 0);
    CHECK(String("HI;HOW#ARE;YOU$DOING%BRO? I;AM#FINE,YOU?").split(";").size() == 4);
    CHECK(String("HI;HOW#ARE;YOU$DOING%BRO? I;AM#FINE,YOU?").split(";#").size() == 6);

    // Search tests
    String s;
    CHECK(s.length() == 0);

    s = String("HELLO YOU !");
    CHECK(s.search("HELLO") == s.data());
    CHECK(s.search("YOU") == s.data() + 6);
    CHECK(s.search("ME") == 0);
    CHECK(s.search("!") == s.data() + s.length() - 1);
    CHECK(String("").search("!") == 0);

    // Vector split test
    s = "a simple string";
    Vector<String> v = s.split(' ');
    CHECK(v.size() == 3);
    CHECK(v[0] == "a");
    CHECK(v[1] == "simple");
    CHECK(v[2] == "string");

    // Find tests
    CHECK(empty.find_left(';') == String::npos);
    CHECK(String("HELLO").find_left(';') == String::npos);
    CHECK(String("HELLO;YOU").find_left(';') == 5);
    CHECK(String("HELLO;YOU").find_left('L') == 2);
    CHECK(String("HELLO;YOU").find_left("L") == 2);
    CHECK(String("HELLO;YOU").find_left("L", 3) == 3);
    CHECK(String("HELLO;YOU").find_left("L", 4) == String::npos);

    CHECK(empty.find_right(';') == String::npos);
    CHECK(String("HELLO;YOU").find_right('L') == 3);
    CHECK(String("HELLO;YOU").find_right('L', 4) == 3);
    CHECK(String("HELLO;YOU").find_right('L', 1) == String::npos);

    CHECK(empty.find_first_of(";") == String::npos);
    CHECK(String("HELLO;YOU").find_first_of("L") == 2);
    CHECK(String("HELLO;YOU").find_first_of("LEH") == 0);
    CHECK(String("HELLO;YOU").find_first_of("U") == 8);
    CHECK(String("HELLO;YOU").find_first_of("L", 4) == String::npos);
    CHECK(String("HELLO;YOU").find_first_of((const char *) "U;") == 5);
    CHECK(String("HELLO;YOU").find_first_of((const String &) "U;") == 5);

    CHECK(empty.find_first_not_of(";") == String::npos);
    CHECK(String("HELLO;YOU").find_first_not_of("HELA") == 4);
    CHECK(String("HELLO;YOU").find_first_not_of("HELLO") == 5);
    CHECK(String("HELLO;YOU").find_first_not_of((const char *) "HELLO;YO") == 8);
    CHECK(String("HELLO;YOU").find_first_not_of((const String &) "HELLO;YO") == 8);

    CHECK(empty.find_last_of(';') == String::npos);
    CHECK(String("HELLO;YOU").find_last_of('L') == 3);
    CHECK(String("HELLO;YOU").find_last_of('O') == 7);
    CHECK(String("HELLO;YOU").find_last_of('L', 1) == String::npos);
    CHECK(String("HELLO;YOU").find_last_of('O', 3) == String::npos);
    CHECK(String("HELLO;YOU").find_last_of('U', 7) == String::npos);
    CHECK(String("HELLO;YOU").find_last_of('U', -2) == 8);

    CHECK(empty.find_last_not_of(";") == String::npos);
    CHECK(String("HELLO;YOU").find_last_not_of("HELLO") == 8);
    CHECK(String("HELLO;YOU").find_last_not_of("HELLO", 7) == 6);
    CHECK(String("HELLO;YOU").find_last_not_of("HELLO", 9) == 8);

    // Erase tests
    CHECK(empty.erase(0, 3) == String::make_empty());
    CHECK(String("HELLO;YOU").erase(5, 4) == "HELLO");
    CHECK(String("HELLO;YOU").erase(0, 6) == "YOU");
    CHECK(String("HELLO;YOU").erase(0, 5).erase(1, 3) == ";");
    CHECK(String("HELLO;YOU").erase(8, 2) == "HELLO;YO");      // 2 gets adjusted to 1
    CHECK(String("HELLO;YOU").erase(8, 0) == "HELLO;YOU");
    CHECK(String("HELLO;YOU").erase(9, 1) == "HELLO;YOU");
    CHECK(String("HELLO;YOU").erase(String::npos, 3) == "HELLO;YOU");
    CHECK(String("HELLO;YOU").erase() == empty);               // Defaults to the whole string
    s = "HELLO;YOU";
    String s1("I;AM;FINE;DUDE");
    s = s.erase();
    CHECK(s == empty);
    s1 = s1.erase();
    CHECK(s1 == empty);
    CHECK(s == s1);

    // Pop back (effectively uses erase)
    empty.pop_back();
    CHECK(empty == String::make_empty());
    String s2("HELLO;YOU");
    String s3("I;AM;FINE;DUDE");
    s2.pop_back();
    CHECK(s2 == "HELLO;YO");
    s2.clear();
    CHECK(s2 == empty);
    s3.clear();
    CHECK(s3 == empty);
    CHECK(s2 == s3);

    if (!errh->nerrors()) {
        errh->message("All tests pass!");
        return 0;
    } else
        return -1;
}

EXPORT_ELEMENT(StringTest)
ELEMENT_REQUIRES(userlevel)
CLICK_ENDDECLS
