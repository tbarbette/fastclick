// -*- c-basic-offset: 4 -*-
/*
 * hashtabletest.{cc,hh} -- regression test element for HashTableMP<K, V>
 * Tom Barbette
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
#include "hashtablemptest.hh"
#include <click/hashtablemp.hh>
#include <click/error.hh>
#include <click/etheraddress.hh>
#include <click/ipaddress.hh>
#if CLICK_USERLEVEL
# include <sys/time.h>
# include <sys/resource.h>
# include <unistd.h>
#endif
CLICK_DECLS

HashTableMPTest::HashTableMPTest()
{
}

#define CHECK(x) if (!(x)) return errh->error("%s:%d: test `%s' failed", __FILE__, __LINE__, #x);
#define CHECK_DATA(x, y, l) CHECK(memcmp((x), (y), (l)) == 0)

int
HashTableMPTest::initialize(ErrorHandler *errh)
{

	AgingTable<IPAddress, EtherAddress, int> cache(10);
	IPAddress ip = IPAddress("10.0.0.1");
	EtherAddress eth;
	EtherAddressArg().parse("aa:bb:cc:dd:ee:ff",eth);
	cache.insert(ip, 1, eth);
	CHECK(cache.size() == 1);

	EtherAddress lookup;
	CHECK(cache.find(ip,1,lookup,false));
	CHECK(lookup == eth);
	CHECK(cache.find(ip,10,lookup,false));
	CHECK(cache.size() == 1);
	CHECK(!cache.find(ip,12,lookup));
	CHECK(cache.size() == 0);

	cache.insert(ip, 21, eth);
	CHECK(cache.size() == 1);
	CHECK(cache.find(ip,30,lookup,true));
	CHECK(cache.find(ip,32,lookup,true));
	CHECK(cache.size() == 1);

    errh->message("All tests pass!");
    return 0;
}

EXPORT_ELEMENT(HashTableMPTest)
CLICK_ENDDECLS
