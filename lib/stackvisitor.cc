#include <click/config.h>
#include <click/glue.hh>
#include <click/element.hh>
#include <click/routervisitor.hh>
#include <click/stackvisitor.hh>

StackVisitor::StackVisitor(StackElement* startElement)
{
    this->startElement = startElement;
}

bool StackVisitor::visit(Element *e, bool isoutput, int port, Element *from_e, int from_port, int distance)
{
    // Check that the element is a stack element
    if(!StackElement::isStackElement(e))
        return true;

    StackElement *element = (StackElement*)e;
    // Only add stack functions to input elements
    if(element->isOutElement())
        return true;

    click_chatter("Adding element %s to the list of %s", startElement->class_name(), element->class_name());

    element->addStackElementInList(startElement);

    // Stop search when finding the IPOut Element
    if(strcmp(element->class_name(), "IPOut") == 0)
        return false;

    return true;
}

StackVisitor::~StackVisitor()
{

}
