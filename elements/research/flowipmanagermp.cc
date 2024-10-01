/*
 * FlowIPManagerMP.{cc,hh} - Thread-safe version of FlowIPManager
 *
 * Copyright (c) 2019-2020 Tom Barbette, KTH Royal Institute of Technology
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
#include <click/glue.hh>
#include "flowipmanagermp.hh"
#include <rte_hash.h>

CLICK_DECLS

FlowIPManagerMP::FlowIPManagerMP()
{
    _flags = RTE_HASH_EXTRA_FLAGS_MULTI_WRITER_ADD | RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY_LF;
}

FlowIPManagerMP::~FlowIPManagerMP()
{
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(dpdk FlowIPManager)
EXPORT_ELEMENT(FlowIPManagerMP)
ELEMENT_MT_SAFE(FlowIPManagerMP)
