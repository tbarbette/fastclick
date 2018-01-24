// -*- c-basic-offset: 4; related-file-name: "flowdirectorparser.hh" -*-
/*
 * flowdirectorparser.cc -- element that relays flow rule instructions
 * to DPDK's flow parsing library.
 *
 * Copyright (c) 2018 Georgios Katsikas, RISE SICS AB
 * Copyright (c) 2018 Tom Barbette, University of Liège
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

#include <click/flowdirectorparser.hh>

CLICK_DECLS

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)

/**
 * Flow Director parsing implementation.
 */

struct cmdline *
flow_parser_init(ErrorHandler *errh)
{
	errh->message("Initializing flow parser...");
	init_port();
	return flow_parser_alloc("", errh);
}

struct cmdline *
flow_parser_alloc(const char *prompt, ErrorHandler *errh)
{
	if (!prompt) {
		errh->error("Flow parser prompt not provided");
		return NULL;
	}

	cmdline_parse_ctx_t *ctx = cmdline_get_ctx();
	if (!ctx) {
		errh->error("Flow parser context not obtained");
		return NULL;
	}

	return cmdline_new(ctx, prompt, 0, 1);
}

int
flow_parser_parse(struct cmdline *cl, char *input_cmd, ErrorHandler *errh)
{
	if (!cl) {
		errh->error("Flow parser is not initialized");
		return FLOWDIR_ERROR;
	}

	return cmdline_parse(cl, input_cmd);
}

#endif // /* RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0) */

CLICK_ENDDECLS
