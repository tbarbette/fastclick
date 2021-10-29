/*
 * handleragg.{cc,hh} -- aggregate handlers
 * Tom Barbette
 *
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
#include "handleragg.hh"
#include <click/error.hh>
#include <click/confparse.hh>
#include <click/args.hh>

CLICK_DECLS

HandlerAggregate::HandlerAggregate()
{
}

HandlerAggregate::~HandlerAggregate()
{
}


int
HandlerAggregate::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
            .read_all("ELEMENT", _elements)
            .complete() < 0)
        return -1;

    return 0;
}

int
HandlerAggregate::initialize(ErrorHandler *errh)
{
    return 0;
}

enum {h_add = 0, h_avg, h_write};

int
HandlerAggregate::handler(int operation, String &data, Element *e,
			       const Handler *handler, ErrorHandler *errh)
{
    HandlerAggregate *c = (HandlerAggregate *)e;

    double d = 0;

    if (operation == Handler::f_read) {

        int f = (intptr_t)handler->read_user_data();
        for (int i = 0; i < c->_elements.size(); i++) {
            HandlerCall h(data);
            h.initialize_read(c->_elements[i], errh);
            d += atof(h.call_read().c_str());
        }
        switch (f) {
            case h_add:
                data = String( d );
                break;
            case h_avg:
                data = String( d / c->_elements.size() );
                break;
            default:
                data = "<error function "+String(f)+">" ;
                return 1;
        }
    } else {

        int f = (intptr_t)handler->write_user_data();
        for (int i = 0; i < c->_elements.size(); i++) {
            HandlerCall h(data);
            h.initialize_write(c->_elements[i], errh);
            h.call_write();
        }
        switch (f) {

            case h_write:
                data = "";
                break;
            default:
                data = "<error function "+String(f)+">" ;
                return 1;
        }

    }
    return 0;
}

void
HandlerAggregate::add_handlers()
{
    set_handler("add", Handler::f_read | Handler::f_read_param, handler, h_add);
    set_handler("avg", Handler::f_read | Handler::f_read_param, handler, h_avg);
    set_handler("write", Handler::f_write, handler, h_write);
}

CLICK_ENDDECLS

EXPORT_ELEMENT(HandlerAggregate)
