// -*- c-basic-offset: 4 -*-
/*
 * flowdispatcherparser.hh -- DPDK's Flow parsing API integrated into Click.
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

#ifndef CLICK_FLOWDISPATCHERPARSER_HH
#define CLICK_FLOWDISPATCHERPARSER_HH

#include <click/config.h>
#include <click/error.hh>
#include <click/flowdispatcherglue.hh>

#if RTE_VERSION >= RTE_VERSION_NUM(20,2,0,0)

#define FLOWDISP_ERROR   ((int)-1)
#define FLOWDISP_SUCCESS ((int) 0)

CLICK_DECLS

/**
 * DPDK's Flow parsing API.
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
		rte_exit(EXIT_FAILURE, "rte_zmalloc(%d struct rte_port) failed\n", RTE_MAX_ETHPORTS);
	}
}

/**
 * Obtains an instance of the Flow Dispatcher parser.
 *
 * @args errh: an instance of the error handler
 * @return a parser object
 */
struct cmdline *flow_parser_init(
	ErrorHandler *errh
);

/**
 * Creates an instance of the Flow Dispatcher parser
 * on a given context of instructions, obtained
 * from DPDK.
 *
 * @args prompt: a user prompt message
 * @args errh: an instance of the error handler
 * @return a command line object
 */
struct cmdline *flow_parser_alloc(
	const char *prompt,
	ErrorHandler *errh
);


/**
 * Parses a new line.
 *
 * @args line: a buffer to store the newly-parsed line
 * @args n: the length of the line buffer
 * @args input_cmd: the input command to parse
 * @return the number of characters read
 */
char *flow_parser_parse_new_line(char *line, int n, const char **input_cmd);

/**
 * Splits a given command into multiple newline-separated tokens
 * and parses each token at a time.
 *
 * @args cl: a flow parser instance
 * @args input_cmd: the input command to parse
 * @args errh: an instance of the error handler
 * @return the number of characters read in total
 */
int flow_parser_parse(struct cmdline *cl, const char *input_cmd, ErrorHandler *errh);

CLICK_ENDDECLS

#endif /* RTE_VERSION >= RTE_VERSION_NUM(20,2,0,0) */

#endif /* CLICK_FLOWDISPATCHERPARSER_HH */
