// -*- c-basic-offset: 4 -*-
/*
 * flowdirectorparser.hh -- Flow Director parsing API between
 * Click and DPDK.
 *
 * Copyright (c) 2018 Georgios Katsikas, RISE SICS
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

#ifndef CLICK_FLOWDIRECTORPARSER_HH
#define CLICK_FLOWDIRECTORPARSER_HH

#include <click/config.h>
#include <click/error.hh>
#include <click/flowdirectorglue.hh>

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)

#define FLOWDIR_ERROR   ((int)-1)
#define FLOWDIR_SUCCESS ((int) 0)

CLICK_DECLS

/**
 * Flow Director parsing API.
 */

/**
 * Allocates memory for storing port information.
 * Copied from RTE_SDK/app/test-pmd/testpmd.c.
 */
static void
init_port(void)
{
	/* Configuration of Ethernet ports. */
	ports = (struct rte_port *) rte_zmalloc("fastclick: ports",
			    sizeof(struct rte_port) * RTE_MAX_ETHPORTS,
			    RTE_CACHE_LINE_SIZE);
	if (ports == NULL) {
		rte_exit(EXIT_FAILURE,
				"rte_zmalloc(%d struct rte_port) failed\n",
				RTE_MAX_ETHPORTS);
	}
}

/**
 * Obtains an instance of the Flow Director parser.
 *
 * @param errh an instance of the error handler
 * @return a parser object
 */
struct cmdline *flow_parser_init(
	ErrorHandler *errh
);

/**
 * Creates an instance of the Flow Director parser
 * on a given context of instructions, obtained
 * from DPDK.
 *
 * @param prompt a user prompt message
 * @param errh an instance of the error handler
 * @return a command line object
 */
struct cmdline *flow_parser_alloc(
	const char *prompt,
	ErrorHandler *errh
);

/**
 * Parses a given command.
 *
 * @param cl the command line instance
 * @param input_cmd the input commandto be parsed
 * @param errh an instance of the error handler
 * @return the status of the parsing
 */
int flow_parser_parse(
	struct cmdline *cl,
	char *input_cmd,
	ErrorHandler *errh
);

CLICK_ENDDECLS

#endif /* RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0) */

#endif /* CLICK_FLOWDIRECTORPARSER_HH */
