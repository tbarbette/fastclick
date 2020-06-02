// -*- c-basic-offset: 4; related-file-name: "flowruleparser.hh" -*-
/*
 * flowruleparser.cc -- relays flow rule instructions
 * to DPDK's flow parsing library.
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

#include <click/config.h>
#include <click/error.hh>
#include <click/flowruleparser.hh>

CLICK_DECLS

#if RTE_VERSION >= RTE_VERSION_NUM(20,2,0,0)

/**
 * DPDK's flow parsing implementation.
 */
struct cmdline *
flow_rule_parser_init(ErrorHandler *errh)
{
    errh->message("Initializing DPDK Flow Parser...");
    init_port();
    return flow_rule_parser_alloc("", errh);
}

struct cmdline *
flow_rule_parser_alloc(const char *prompt, ErrorHandler *errh)
{
    if (!prompt) {
        errh->error("DPDK Flow Parser prompt not provided");
        return NULL;
    }

    cmdline_parse_ctx_t *ctx = cmdline_get_ctx();
    if (!ctx) {
        errh->error("DPDK Flow Parser context not obtained");
        return NULL;
    }

    return cmdline_new(ctx, prompt, 0, 1);
}

char *
flow_rule_parser_parse_new_line(char *line, int n, const char **input_cmd)
{
    // End of input
    if(**input_cmd == '\0') {
        return NULL;
    }

    int i;
    for(i=0; i < n-1; ++i, ++(*input_cmd)) {
        line[i] = **input_cmd;
        if (**input_cmd == '\0') {
            break;
        }

        // End of line
        if (**input_cmd == '\n') {
            line[i+1] = '\0';
            ++(*input_cmd);
            break;
        }
    }

    if (i == n-1) {
        line[i] = '\0';
    }

    return line;
}

int
flow_rule_parser_parse(struct cmdline *cl, const char *input_cmd, ErrorHandler *errh)
{
    if (!cl) {
        errh->error("DPDK Flow Parser is not initialized");
        return FLOWRULEPARSER_ERROR;
    }

    char buff[512];
    const char **p = &input_cmd;
    int tot_line_len = 0;

    // Split the input command in lines
    while (flow_rule_parser_parse_new_line(buff, sizeof(buff), p) != NULL) {
        int line_len;
        if ((line_len = cmdline_parse(cl, buff)) < 0) {
            errh->error("DPDK Flow Parser failed to parse input line: %s\n", buff);
            return FLOWRULEPARSER_ERROR;
        }
        tot_line_len += line_len;
    }

    // This is the last line to parse
    return tot_line_len;
}

#endif // /* RTE_VERSION >= RTE_VERSION_NUM(20,2,0,0) */

CLICK_ENDDECLS
