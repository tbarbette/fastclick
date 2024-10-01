/*
 * FlowHyperScan.{cc,hh} Flow-based IDS using the HyperScan library
 *
 * This file integrates some code taken from the hyperscan helper, the license for that code is:
 *
 * Copyright (c) 2015-2016, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * The rest of the code is using the usual Click license :
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
#include <click/args.hh>
#include <click/flow/flow.hh>
#include <click/userutils.hh>
#include "flowhyperscan.hh"

CLICK_DECLS

FlowHyperScan::FlowHyperScan() : db_streaming(0)
{
    _scratch = 0;
};

FlowHyperScan::~FlowHyperScan()
{

}

static hs_database_t *buildDatabase(const Vector<const char *> &expressions,
                                    const Vector<unsigned> flags,
                                    const Vector<unsigned> ids,
                                    unsigned int mode) {
    hs_database_t *db;
    hs_compile_error_t *compileErr;
    hs_error_t err;

    err = hs_compile_multi(expressions.data(), flags.data(), ids.data(),
                           expressions.size(), mode, nullptr, &db, &compileErr);
    if (err != HS_SUCCESS) {
        if (compileErr->expression < 0) {
            // The error does not refer to a particular expression.
            click_chatter("ERROR %d: %s",err, compileErr->message);
        } else {
            click_chatter("ERROR %d: Pattern %d '%s' failed compilation with error: %s", err, compileErr->expression, expressions[compileErr->expression], compileErr->message);

        }
        // As the compileErr pointer points to dynamically allocated memory, if
        // we get an error, we must be sure to release it. This is not
        // necessary when no error is detected.
        hs_free_compile_error(compileErr);
        return 0;
    }

    return db;
}

/*
static void parseFile(const char *filename, vector<string> &patterns,
                      vector<unsigned> &flags, vector<unsigned> &ids)
{
    ifstream inFile(filename);
    if (!inFile.good()) {
        cerr << "ERROR: Can't open pattern file \"" << filename << "\"" << endl;
        exit(-1);
    }

    for (unsigned i = 1; !inFile.eof(); ++i) {
        string line;
        getline(inFile, line);

        // if line is empty, or a comment, we can skip it
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // otherwise, it should be ID:PCRE, e.g.
        //  10001:/foobar/is

        size_t colonIdx = line.find_first_of(':');
        if (colonIdx == string::npos) {
            cerr << "ERROR: Could not parse line " << i << endl;
            exit(-1);
        }

        // we should have an unsigned int as an ID, before the colon
        unsigned id = std::stoi(line.substr(0, colonIdx).c_str());

        // rest of the expression is the PCRE
        const string expr(line.substr(colonIdx + 1));

        size_t flagsStart = expr.find_last_of('/');
        if (flagsStart == string::npos) {
            cerr << "ERROR: no trailing '/' char" << endl;
            exit(-1);
        }

        string pcre(expr.substr(1, flagsStart - 1));
        string flagsStr(expr.substr(flagsStart + 1, expr.size() - flagsStart));
        unsigned flag = parseFlags(flagsStr);

        patterns.push_back(pcre);
        flags.push_back(flag);
        ids.push_back(id);
    }
}
*/

bool
FlowHyperScan::is_valid_patterns(Vector<String> &patterns, ErrorHandler *errh)
{
    Vector<const char*> test_set;
    Vector<unsigned> flags;
    Vector<unsigned> ids;
    bool valid = true;
    int id = 0;
    for (int i=0; i < patterns.size(); ++i) {
        String pattern = cp_unquote(patterns[i]);
        if (!pattern)
            continue;
        char * p = new char[pattern.length() + 1];
        memcpy(p, pattern.c_str(), pattern.length() + 1);
        test_set.push_back(p);
        flags.push_back(_flags);
        ids.push_back(id++);
    }
    if (valid) {
        // Try to compile
        db_streaming = buildDatabase(test_set, flags, ids, HS_MODE_STREAM);
        valid = db_streaming != 0;
    }

    return valid;
}

int
FlowHyperScan::configure(Vector<String> &conf, ErrorHandler *errh)
{
    bool payload_only;
    String flags_s;
    String file = "";
    if (Args(this, errh).bind(conf)
      .read("PAYLOAD_ONLY", payload_only)
      .read("VERBOSE", _verbose)
      .read("FLAGS", flags_s)
      .read("FILE", file)
      .read_or_set("KILL", _kill, false)
      .consume() < 0)
      return -1;

    unsigned flags = 0;
    for (const auto &c : flags_s) {
        switch (c) {
        case 'i':
            flags |= HS_FLAG_CASELESS; break;
        case 'm':
            flags |= HS_FLAG_MULTILINE; break;
        case 's':
            flags |= HS_FLAG_DOTALL; break;
        case 'H':
            flags |= HS_FLAG_SINGLEMATCH; break;
        case 'V':
            flags |= HS_FLAG_ALLOWEMPTY; break;
        case '8':
            flags |= HS_FLAG_UTF8; break;
        case 'W':
            flags |= HS_FLAG_UCP; break;
        case '\r': // stray carriage-return
            break;
        default:
            return errh->error("Unsupported flag %c", c);
        }
    }

    _flags = flags;
    _payload_only = payload_only;


    if (file) {
        file_read_lines(file,conf);
    }

    if (!is_valid_patterns(conf, errh)) {
        return -1;
    }

    return 0;
}

int FlowHyperScan::initialize(ErrorHandler *errh)
{
/*    hs_error_t err = hs_alloc_scratch(db_streaming, &_scratch);
    if (err != HS_SUCCESS) {
        return errh->error("ERROR: could not allocate scratch space. Error %d",err);
    }*/
    for (int i =0; i < _state.weight();i ++) {
//        _state.get_value(i).scratch = _scratch;
        // Allocate enough scratch space to handle either streaming or block
        // mode, so we only need the one scratch region.
        _state.get_value(i).scratch = 0;
        hs_error_t err = hs_alloc_scratch(db_streaming, &_state.get_value(i).scratch);
        if (err != HS_SUCCESS) {
            return errh->error("ERROR: could not allocate scratch space. Error %d",err);
        }
    }

    return 0;
}

void FlowHyperScan::cleanup(CleanupStage) {
    if (db_streaming)
        hs_free_database(db_streaming);
}

// Match event handler: called every time Hyperscan finds a match.
static
int onMatch(unsigned int id, unsigned long long from, unsigned long long to,
            unsigned int flags, void *ctx)
{
    // Our context points to a size_t storing the match count
    size_t *matches = (size_t *)ctx;
    (*matches)++;
    return 0; // continue matching
}

void FlowHyperScan::push_flow(int port, FlowHyperScanState* flowdata, PacketBatch* batch)
{
    if (!flowdata->stream) {
        hs_error_t err = hs_open_stream(db_streaming, 0, &flowdata->stream);
        if (err) {
            click_chatter("Cannot alloc stream!");
            goto err;
        }
    } else if (unlikely(flowdata->found)) {
        if (_kill)
            goto err;
        output_push_batch(0, batch);
        return;
    }

    FOR_EACH_PACKET(batch, p) {
        if (p->length() == 0) continue;
        size_t matchCount = 0;

        hs_error_t err = hs_scan_stream(flowdata->stream,
        reinterpret_cast<const char*>(p->data()), p->length(), 0,
        _state->scratch, onMatch, &matchCount);
        if (unlikely(err != HS_SUCCESS)) {
            if (err == HS_SCAN_TERMINATED) {
                flowdata->found = true;
                goto m;
            }
            click_chatter("Matching error");
            hs_reset_stream(flowdata->stream, 0, _state->scratch, 0, 0);

        }
    m:
        if (matchCount > 0) {
            if (_verbose)
                click_chatter("MATCHED");
            _state->matches++;
        }
        if (_kill)
            goto err;

    }
    output_push_batch(0, batch);

    return;
    err:
        batch->kill();

}


CLICK_ENDDECLS

ELEMENT_REQUIRES(hs)
EXPORT_ELEMENT(FlowHyperScan)
ELEMENT_MT_SAFE(FlowHyperScan)
