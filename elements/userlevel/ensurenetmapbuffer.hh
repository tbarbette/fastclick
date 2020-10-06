// -*- c-basic-offset: 4 -*-
#ifndef CLICK_ENSURENETMAPBUFFER_HH
#define CLICK_ENSURENETMAPBUFFER_HH
#include <click/config.h>
#include <click/batchelement.hh>

CLICK_DECLS

/*
 * =c
 *
 * EnsureNetmapBuffer()

 */


class EnsureNetmapBuffer: public BatchElement  {

public:
	EnsureNetmapBuffer() CLICK_COLD;
	~EnsureNetmapBuffer() CLICK_COLD;

    const char *class_name() const override		{ return "EnsureNetmapBuffer"; }
    const char *port_count() const override		{ return PORTS_1_1; }
    const char *processing() const override		{ return AGNOSTIC; }

    int configure(Vector<String> &conf, ErrorHandler *errh);

    Packet* smaction(Packet*);
#if HAVE_BATCH
    PacketBatch* simple_action_batch(PacketBatch*);
#endif
    Packet* simple_action(Packet*);
    int _headroom;
};

CLICK_ENDDECLS
#endif
