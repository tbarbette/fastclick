// -*- c-basic-offset: 4 -*-
/*
 * flowruleparser.hh -- glues Click and DPDK for
 * flow parsing and installation on DPDK-based NICs.
 *
 * Copyright (c) 2018 Tom Barbette, University of Liège
 * Copyright (c) 2018 Georgios Katsikas, RISE SICS
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

#ifndef CLICK_FLOWRULEPARSER_H
#define CLICK_FLOWRULEPARSER_H

CLICK_DECLS

class ErrorHandler;

#ifdef __cplusplus
extern "C" {
#endif

#include <rte_version.h>

#if HAVE_FLOW_API

#include <rte_malloc.h>
#include <rte_ethdev.h>
#include <rte_flow.h>
#include <cmdline.h>
#include <cmdline_parse.h>
extern "C" {
#include <testpmd.h>
}
/**
 * Denotes that portid_t is defined
 * in <testpmd.h>.
 * DPDKDevice will use this or define
 * its own (depending on the DPDK version).
 */
#define PORTID_T_DEFINED

/**
 * External reference to the ports array
 * defined in app/test-pmd/testpmd.c.
 * This array is crucial for the successful
 * call of the port_flow_*() functions in
 * app/test-pmd/config.c.
 */
extern struct rte_port *ports;

/**
 *  Returns the global instance for all ports.
 */
static struct rte_port *get_ports() {
    return ports;
}

/**
 *  Returns the global instance for a given port.
 */
static struct rte_port *get_port(const portid_t &port_id) {
    if (!ports) {
        printf(
            "DPDK Flow Parser (port %u): Unallocated DPDK ports\n",
            port_id
        );
        return NULL;
    }

    if (port_id < 0) {
        printf(
            "DPDK Flow Parser (port %u): Invalid port identifier\n",
            port_id
        );
        return NULL;
    }

    return &ports[port_id];
}

/**
 * External reference to the main_ctx array
 * defined in app/test-pmd/cmdline.c.
 * This array is crucial for the successful
 * call of the cmdline_new() function below.
 */
extern cmdline_parse_ctx_t main_ctx[];

/**
 *  Defined: Nowhere, but we need it to acquire main_ctx
 * Declared: Nowhere, but we need it to acquire main_ctx
 */
static cmdline_parse_ctx_t *cmdline_get_ctx() {
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

#endif // /* HAVE_FLOW_API */

#ifdef __cplusplus
}

#define FLOWRULEPARSER_ERROR   ((int)-1)
#define FLOWRULEPARSER_SUCCESS ((int) 0)

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
 * Obtains an instance of the Flow Parser's parser.
 *
 * @args errh: an instance of the error handler
 * @return a parser object
 */
struct cmdline *flow_rule_parser_init(
	ErrorHandler *errh
);

/**
 * Creates an instance of the Flow Parser's parser
 * on a given context of instructions, obtained
 * from DPDK.
 *
 * @args prompt: a user prompt message
 * @args errh: an instance of the error handler
 * @return a command line object
 */
struct cmdline *flow_rule_parser_alloc(
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
char *flow_rule_parser_parse_new_line(char *line, int n, const char **input_cmd);

/**
 * Splits a given command into multiple newline-separated tokens
 * and parses each token at a time.
 *
 * @args cl: a flow parser instance
 * @args input_cmd: the input command to parse
 * @args errh: an instance of the error handler
 * @return the number of characters read in total
 */
int flow_rule_parser_parse(struct cmdline *cl, const char *input_cmd, ErrorHandler *errh);

#endif

CLICK_ENDDECLS

#endif /* CLICK_FLOWRULEPARSER_H */
