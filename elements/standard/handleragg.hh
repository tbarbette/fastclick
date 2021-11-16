// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_HANDLERAGG_HH
#define CLICK_HANDLERAGG_HH
#include <click/element.hh>
#include <click/handlercall.hh>
#include <click/vector.hh>

CLICK_DECLS

/*
=c

HandlerAggregate(ELEMENT 1 [, ELEMENT 2 ... ELEMENT N])

=s control

Aggregates handlers of multiple elements

=d

Gives the sum of the handlers of the given elements, unless there is avg in the name

*/

class HandlerAggregate : public Element { public:

	HandlerAggregate() CLICK_COLD;
    ~HandlerAggregate() CLICK_COLD;

    const char *class_name() const override  {return "HandlerAggregate";};

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;
    static int handler(int operation, String &data, Element *element,
			       const Handler *handler, ErrorHandler *errh);

    void add_handlers() CLICK_COLD;

private:
    Vector<Element*> _elements;
};

CLICK_ENDDECLS
#endif
