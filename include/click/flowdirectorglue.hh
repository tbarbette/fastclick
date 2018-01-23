// -*- c-basic-offset: 4 -*-
/*
 * flowdirectorglue.hh -- element that glues Click and DPDK for
 * flow parsing and installation on DPDK-based NICs.
 *
 * Copyright (c) 2018 Tom Barbette, University of Liège
 * Copyright (c) 2018 Georgios Katsikas, RISE SICS AB
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

#ifndef _CLICK_FLOWDIRECTOR_GLUE_H_
#define _CLICK_FLOWDIRECTOR_GLUE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <rte_flow.h>
#include <cmdline.h>
#include <cmdline_parse.h>

/**
 * Build instructions for gluing with DPDK.
 *
 * Direct connection with DPDK:
 * We need the following source files from DPDK:
 * \-> app/test-pmd/cmdline.c
 *     Contains object list of instructions cmdline_parse_ctx_t main_ctx[].
 *     This object must be used as 1st argument in cmdline_new below.
 *     To do so, we can write a simple getter as follows:
 *     cmdline_parse_ctx_t *main_ctx = cmdline_get_ctx();
 *     and then call cmdline_new(main_ctx, ...) as I do in
 *     flow_parser_alloc() (in lib/flowdirectorparser.cc)
 * \-> lib/librte_cmdline/cmdline.c
 *     Provides cmdline_new(main_ctx, ...)
 *     Returns a parser (struct cmdline *) instance
 *     Given that we got main_ctx, this is trivial call.
 * |-> lib/librte_cmdline/cmdline_parse.c
 *     Provides cmdline_parse(cl, input_cmd)
 *     Uses the parser instance (struct cmdline *) returned by
 *     cmdline_new() above.
 */

/**
 *  Defined: Nowhere, but we need it in app/test-pmd/cmdline.c
 * Declared: Nowhere, but we need it in app/test-pmd/cmdline.c
 *  Comment: You can create a app/test-pmd/cmdline.h with this definition only
 */
extern cmdline_parse_ctx_t main_ctx[];

cmdline_parse_ctx_t *cmdline_get_ctx() {
    return main_ctx;
}

/**
 *  Defined: lib/librte_cmdline/cmdline.h
 * Declared: lib/librte_cmdline/cmdline.c
 * Requires: The object returned from cmdline_get_ctx() above as 1st argument
 */
struct cmdline *cmdline_new(
	cmdline_parse_ctx_t *ctx,
	const char *prompt,
	int s_in,
	int s_out
);

/**
 *  Defined: lib/librte_cmdline/cmdline_parse.h
 * Declared: lib/librte_cmdline/cmdline_parse.c
 * Requires: The object returned from cmdline_new() above as 1st argument
 */
int cmdline_parse(
	struct cmdline *cl,
	const char *buf
);

/**
 * Implicit connections with DPDK:
 * Apart from the files above, you will also need the following
 * source files:
 * |-> The whole librte_cmdline library
 * |-> app/test-pmd/testpmd.c
 * |-> app/test-pmd/config.c
 * |-> app/test-pmd/cmdline_flow.c
 * |-> app/test-pmd/cmdline_mtr.c
 * |-> app/test-pmd/cmdline_tm.c
 * Note that the last two files appear only after DPDK 17.11,
 * so we need to put RTE_VERSION guardians.
 */

#ifdef __cplusplus
}
#endif

#endif /* _CLICK_FLOWDIRECTOR_GLUE_H_ */
