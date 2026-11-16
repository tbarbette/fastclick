// -*- c-basic-offset: 4; related-file-name: "../include/click/routervisitor.hh" -*-
/*
 * routervisitor.{cc,hh} -- element filters
 * Eddie Kohler
 *
 * Copyright (c) 2009 Meraki, Inc.
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
#include <click/routervisitor.hh>
#include <click/router.hh>
#include <click/error.hh>
CLICK_DECLS

/** @file routervisitor.hh
 * @brief Router configuration visitors for finding elements and ports.
 */

bool
RouterVisitor::visit(Element *, bool, int, Element *, int, int)
{
    return true;
}

int
RouterVisitor::distance(Element *e, Element *from_e) {
    return 1;
}

ElementTracker::ElementTracker(Router *router)
    : _reached(router->nelements(), false)
{
}

bool
ElementCastTracker::visit(Element *e, bool, int, Element *, int, int)
{
    if (e->cast(_name.c_str())) {
	insert(e);
	return _continue;
    } else
	return true;
}

bool
ElementNeighborhoodTracker::visit(Element *e, bool, int,
				  Element *, int, int distance)
{
    if (distance <= _diameter)
	insert(e);
    return distance < _diameter;
}


namespace {

// The kinds of packets that may be on a port: a set of these bits.
enum {
    pk_shared = 1,      // shared with clones
    pk_unshared = 2,
    pk_click = 4,       // in a Click buffer
    pk_dpdk = 8         // in a DPDK buffer
};

// The buffer of the packets made by Packet::make() and of the copies made
// by uniqueify()
#if CLICK_PACKET_USE_DPDK || CLICK_PACKET_INSIDE_DPDK || HAVE_DPDK_PACKET_POOL
const int pk_default_buffer = pk_dpdk;
#else
const int pk_default_buffer = pk_click;
#endif

struct PacketKind {
    int bits;
    Element *cloner;    // the element that made the shared packets

    PacketKind() : bits(0), cloner(0) {
    }

    bool merge(const PacketKind &k) {
        int old = bits;
        bits |= k.bits;
        if (!cloner && (bits & pk_shared))
            cloner = k.cloner;
        return bits != old;
    }
};

// Collects the ports connected to one port, without going further
class ConnectedPorts : public RouterVisitor { public:
    Vector<Element *> elements;
    Vector<int> ports;

    bool visit(Element *e, bool, int port, Element *, int, int) {
        elements.push_back(e);
        ports.push_back(port);
        return false;
    }
};

// The packets leaving an output port with code @a code, from packets of
// kind @a in (@a fed is false if no input flows to this output)
PacketKind
output_kind(Element *e, const String &code, const PacketKind &in, bool fed)
{
    if (!code)
        return in;
    PacketKind out;
    for (const char *c = code.begin(); c != code.end(); ++c)
        switch (*c) {
        case 's':
            out.merge(in);
            break;
        case 'c': {
            PacketKind k;
            if (fed && in.bits)
                k.bits = (in.bits & (pk_click | pk_dpdk)) | pk_shared;
            else if (!fed)
                k.bits = pk_default_buffer | pk_shared;
            k.cloner = e;
            out.merge(k);
            break;
        }
        case 'n': {
            PacketKind k;
            k.bits = pk_default_buffer | pk_unshared;
            out.merge(k);
            break;
        }
        case 'm': {
            PacketKind k;
            k.bits = pk_dpdk | pk_unshared;
            out.merge(k);
            break;
        }
        case 'd': {
            PacketKind k;
            if (in.bits & pk_dpdk) {
                k.bits = (in.bits & (pk_shared | pk_unshared)) | pk_dpdk;
                k.cloner = in.cloner;
            }
            if (in.bits & pk_click)
                k.bits |= pk_dpdk | pk_unshared;
            out.merge(k);
            break;
        }
        }
    return out;
}

}

void
PacketKindChecker::check(Router *router, ErrorHandler *errh)
{
#ifdef CLICK_NOINDIRECT
    // Clones are copies: no packet is ever shared, and writers never copy
    (void) router;
    (void) errh;
    return;
#endif
    int n = router->nelements();
    Vector<Vector<PacketKind> > in(n, Vector<PacketKind>()), out(n, Vector<PacketKind>());
    for (int i = 0; i < n; i++) {
        in[i].resize(router->element(i)->ninputs());
        out[i].resize(router->element(i)->noutputs());
    }

    // Propagate the kinds until nothing changes. A port's kind only gains
    // bits, so this ends even if the configuration has loops.
    Vector<int> work;
    Vector<bool> queued(n, true);
    for (int i = n - 1; i >= 0; i--)
        work.push_back(i);
    Bitvector flow;
    while (work.size()) {
        int i = work.back();
        work.pop_back();
        queued[i] = false;
        Element *e = router->element(i);

        Vector<PacketKind> eout(e->noutputs(), PacketKind());
        Vector<bool> fed(e->noutputs(), false);
        for (int port = 0; port < e->ninputs(); port++) {
            PacketKind k = in[i][port];
            // A writer copies the shared packets, into the default buffer
            if ((k.bits & pk_shared)
                && e->port_packet_code(false, port).find_left('w') >= 0) {
                k.bits = (k.bits & ~pk_shared) | pk_unshared | pk_default_buffer;
                k.cloner = 0;
            }
            e->port_flow(false, port, &flow);
            for (int o = 0; o < e->noutputs(); o++)
                if (flow[o]) {
                    fed[o] = true;
                    eout[o].merge(output_kind(e, e->port_packet_code(true, o), k, true));
                }
        }

        for (int o = 0; o < e->noutputs(); o++) {
            if (!fed[o])
                eout[o] = output_kind(e, e->port_packet_code(true, o), PacketKind(), false);
            if (!out[i][o].merge(eout[o]))
                continue;
            ConnectedPorts next;
            router->visit(e, true, o, &next);
            for (int j = 0; j < next.elements.size(); j++) {
                int ni = next.elements[j]->eindex();
                if (in[ni][next.ports[j]].merge(out[i][o]) && !queued[ni]) {
                    queued[ni] = true;
                    work.push_back(ni);
                }
            }
        }
    }

    for (int i = 0; i < n; i++) {
        Element *e = router->element(i);
        for (int port = 0; port < e->ninputs(); port++) {
            const PacketKind &k = in[i][port];
            if ((k.bits & pk_shared)
                && e->port_packet_code(false, port).find_left('w') >= 0)
                errh->warning("%p{element} writes into packets shared with clones%s%s, so it copies each of them%s. Unshare them before it, or write before cloning.",
                              e, k.cloner ? " from " : "", k.cloner ? k.cloner->declaration().c_str() : "",
#if CLICK_PACKET_USE_DPDK
                              ""
#else
                              " with its whole buffer"
#endif
                              );
        }
    }
}

CLICK_ENDDECLS
