/*
 * checkipheader.{cc,hh} -- element checks IP header for correctness
 * (checksums, lengths, source addresses)
 * Robert Morris, Eddie Kohler
 *
 * Computational batching support and counter & handler updates
 * by Georgios Katsikas
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2003 International Computer Science Institute
 * Copyright (c) 2020 UBITECH and KTH Royal Institute of Technology
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
#include "checkipheader.hh"
#include <clicknet/ip.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/straccum.hh>
#include <click/error.hh>
#include <click/standard/alignmentinfo.hh>

CLICK_DECLS

const char * const CheckIPHeader::reason_texts[NREASONS] = {
    "tiny packet", "bad IPv4 version", "bad IPv4 header length",
    "bad IPv4 length", "bad IPv4 checksum", "bad source address"
};

#define IPADDR_LIST_INTERFACES ((void *)0)
#define IPADDR_LIST_BADSRC     ((void *)1)
#define IPADDR_LIST_BADSRC_OLD ((void *)2)

bool
CheckIPHeader::OldBadSrcArg::parse(const String &str, Vector<IPAddress> &result, Args &args)
{
    if (IPAddressArg().parse(str, result, args)) {
        result.push_back(IPAddress(0));
        result.push_back(IPAddress(0xFFFFFFFFU));
        return true;
    } else {
        return false;
    }
}

bool
CheckIPHeader::InterfacesArg::parse(const String &str, Vector<IPAddress> &result_bad_src,
    Vector<IPAddress> &result_good_dst, Args &args)
{
    String arg(str);
    IPAddress ip, mask;
    int nwords = 0;
    while (String word = cp_shift_spacevec(arg)) {
        ++nwords;
        if (IPPrefixArg(true).parse(word, ip, mask, args)) {
            result_bad_src.push_back((ip & mask) | ~mask);
            result_good_dst.push_back(ip);
        } else {
            return false;
        }
    }

    if (nwords == result_bad_src.size()) {
        result_bad_src.push_back(IPAddress(0));
        result_bad_src.push_back(IPAddress(0xFFFFFFFFU));
        return true;
    }

    args.error("out of memory");
    return false;
}

CheckIPHeader::CheckIPHeader() : _checksum(true), _reason_drops(0)
{
    _count = 0;
    _drops = 0;
}

CheckIPHeader::~CheckIPHeader()
{
    if (_reason_drops) {
        delete _reason_drops;
    }
}

int
CheckIPHeader::configure(Vector<String> &conf, ErrorHandler *errh)
{
    _offset = 0;
    bool verbose = false;
    bool details = false;

    if (Args(this, errh).bind(conf)
        .read("INTERFACES", InterfacesArg(), _bad_src, _good_dst)
        .read("BADSRC", _bad_src)
        .read("GOODDST", _good_dst)
        .read("OFFSET", _offset)
        .read("VERBOSE", verbose)
        .read("DETAILS", details)
        .read("CHECKSUM", _checksum)
        .consume() < 0)
        return -1;

    _verbose = verbose;
    if (details) {
        _reason_drops = new atomic_uint64_t[NREASONS];
        memset(_reason_drops, 0, NREASONS * sizeof(atomic_uint64_t));
    }

#if HAVE_FAST_CHECKSUM && FAST_CHECKSUM_ALIGNED
    // check alignment
    if (_checksum) {
        int ans, c, o;
        ans = AlignmentInfo::query(this, 0, c, o);
        o = (o + 4 - (_offset % 4)) % 4;
        _aligned = (ans && c == 4 && o == 0);
        if (!_aligned)
            errh->warning("IP header unaligned, cannot use fast IP checksum");
        if (!ans)
            errh->message("(Try passing the configuration through `click-align'.)");
    }
#endif

    return 0;
}

Packet *
CheckIPHeader::drop(Reason reason, Packet *p, bool batch)
{
    if (_drops == 0 || _verbose) {
        click_chatter("%s: IP header check failed: %s", name().c_str(), reason_texts[reason]);
    }
    _drops++;

    if (_reason_drops) {
        _reason_drops[reason]++;
    }

    if (noutputs() == 2) {
        output(1).push(p);
    } else {
        p->kill();
    }

    return 0;
}

inline CheckIPHeader::Reason CheckIPHeader::valid(Packet* p) {
    unsigned plen = p->length() - _offset;

    // cast to int so very large plen is interpreted as negative
    if ((int)plen < (int)sizeof(click_ip))
        return MINISCULE_PACKET;

    const click_ip *ip = reinterpret_cast<const click_ip *>(p->data() + _offset);
    if (ip->ip_v != 4)
        return BAD_VERSION;

    unsigned hlen = ip->ip_hl << 2;
    if (hlen < sizeof(click_ip))
        return BAD_HLEN;

    unsigned len = ntohs(ip->ip_len);
    if (len > plen || len < hlen)
        return BAD_IP_LEN;

    if (_checksum) {
        int val;
    #if HAVE_FAST_CHECKSUM && FAST_CHECKSUM_ALIGNED
        if (_aligned)
            val = ip_fast_csum((unsigned char *)ip, ip->ip_hl);
        else
            val = click_in_cksum((const unsigned char *)ip, hlen);
    #elif HAVE_FAST_CHECKSUM
        val = ip_fast_csum((unsigned char *)ip, ip->ip_hl);
    #else
        val = click_in_cksum((const unsigned char *)ip, hlen);
    #endif
        if (val != 0)
            return BAD_CHECKSUM;
    }

    /*
    * RFC1812 5.3.7 and 4.2.2.11: discard illegal source addresses.
    * Configuration string should have listed all subnet
    * broadcast addresses known to this router.
    */
    if (find(_bad_src.begin(), _bad_src.end(), IPAddress(ip->ip_src)) < _bad_src.end() &&
        find(_good_dst.begin(), _good_dst.end(), IPAddress(ip->ip_dst)) == _good_dst.end())
        return BAD_SADDR;

    /*
    * RFC1812 4.2.3.1: discard illegal destinations.
    * We now do this in the IP routing table.
    */
    p->set_ip_header(ip, hlen);

    // shorten packet according to IP length field -- 7/28/2000
    if (plen > len)
        p->take(plen - len);

    // set destination IP address annotation if it doesn't exist already --
    // 9/26/2001
    // always set destination IP address annotation; linuxmodule problem
    // reported by Parveen Kumar Patel at Utah -- 4/3/2002
    p->set_dst_ip_anno(ip->ip_dst);

    _count++;

    return NREASONS;
}

Packet *
CheckIPHeader::simple_action(Packet *p)
{
    Reason r;
    if ((r = valid(p)) == NREASONS) {
        return p;
    } else {
        drop(r, p, false);
        return NULL;
    }
}

String
CheckIPHeader::read_handler(Element *e, void *thunk)
{
    CheckIPHeader *c = reinterpret_cast<CheckIPHeader *>(e);

    switch (reinterpret_cast<uintptr_t>(thunk)) {
        case h_count: {
            return String(c->_count);
        }
        case h_drops: {
            return String(c->_drops);
        }
        case h_drop_details: {
            StringAccum sa;
            for (unsigned i = 0; i < NREASONS; i++) {
                sa.snprintf(15, "%15" PRIu64, c->_reason_drops[i].value());
                sa << " packets due to: ";
                sa.snprintf(24, "%24s", reason_texts[i]);
                sa << "\n";
            }
            return sa.take_string();
        }
        default: {
            return String();
        }
    }
}

void
CheckIPHeader::add_handlers()
{
    add_read_handler("count", read_handler, h_count);
    add_read_handler("drops", read_handler, h_drops);
    if (_reason_drops)
        add_read_handler("drop_details", read_handler, h_drop_details);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(CheckIPHeader)
ELEMENT_MT_SAFE(CheckIPHeader)
