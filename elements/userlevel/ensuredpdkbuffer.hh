// -*- c-basic-offset: 4 -*-
#ifndef CLICK_ENSUREDPDKBUFFER_HH
#define CLICK_ENSUREDPDKBUFFER_HH
#include <click/config.h>
#include <click/batchelement.hh>

CLICK_DECLS

/*
 * =c
 *
 * EnsureDPDKBuffer()

 */


class EnsureDPDKBuffer: public BatchElement  {

public:
	EnsureDPDKBuffer() CLICK_COLD;
	~EnsureDPDKBuffer() CLICK_COLD;

    const char *class_name() const		{ return "EnsureDPDKBuffer"; }
    const char *port_count() const		{ return PORTS_1_1; }
    const char *processing() const		{ return AGNOSTIC; }
    int configure(Vector<String> &conf, ErrorHandler *errh);

    Packet* smaction(Packet*);
#if HAVE_BATCH
    PacketBatch* simple_action_batch(PacketBatch*);
#endif
    Packet* simple_action(Packet*);

private:
    bool _force;
    int _extra_headroom;
};

CLICK_ENDDECLS
#endif
