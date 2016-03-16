#ifndef MIDDLEBOX_STACKVISITOR_HH
#define MIDDLEBOX_STACKVISITOR_HH

#include "../../elements/userlevel/middlebox/stackelement.hh"
#include <click/vector.hh>
#include <click/string.hh>
#include <click/packet.hh>
#include <click/element.hh>
#include <click/routervisitor.hh>

class StackElement;

class StackVisitor : public RouterVisitor
{
public:
    StackVisitor(StackElement*);
    ~StackVisitor();
    bool visit(Element*, bool, int, Element*, int, int);

private:
    StackElement* startElement;
};

#endif
