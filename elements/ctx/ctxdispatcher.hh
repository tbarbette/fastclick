#ifndef CLICK_CTXDispatcher_HH
#define CLICK_CTXDispatcher_HH
#include <click/string.hh>
#include <click/timer.hh>
#include "ctxmanager.hh"
#include <vector>
#include <click/flow/flowelement.hh>

CLICK_DECLS

/**
 * =c
 * CTXDispatcher
 *
 * =s ctx
 *
 * =d
 * Define a context to classify packets among its outputs. It works much like Classifier, except the context will
 * be grabbed by a CTXManager element that will handle classification and state management. See the MiddleClick paper.
 *
 * It takes a serie of argument such as :
 * CTXDispatcher(12/0806 20/01 0, 12/0806 20/02 1, 12/0800 2, - drop)
 *
 * Where each argument defines a pattern and an output port. The output ports can be omitted if sequential, as much as the default rule.
 * The following is therefore identical.
 * CTXDispatcher(12/0806 20/01, 12/0806 20/02, 12/0800)
 *
 * Each output rule cannot overlap with other rules. Eg you cannot have TCP packets going to port 0 and IP packets to port 1,
 * as TCP packets are IP packets, it is impossible. You can use FlowContextDispatcher to have rules added in a "cascading else" fashion, i.e.
 * if packets are not TCP but still IP packets, go to port 1. This is the default behavior of the context link (~>).
 *
 *
 */
class CTXDispatcher: public FlowSpaceElement<int> {

public:

	CTXDispatcher() CLICK_COLD;
	~CTXDispatcher() CLICK_COLD {};

    const char *class_name() const		{ return "CTXDispatcher"; }
    const char *port_count() const		{ return "1/-"; }
    const char *processing() const		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int solve_initialize(ErrorHandler *errh) CLICK_COLD;

    void push_flow(int, int* flowdata, PacketBatch* batch) override;
    FlowNode* get_table(int,Vector<FlowElement*>) override;

private :

    FlowNode* _table;

    bool _verbose;
    bool attach_children(FlowNodePtr* ptr, int output, bool append_drop,Vector<FlowElement*> context);
    FlowNode* get_child(int output, bool append_drop,Vector<FlowElement*> context);

protected:

    Vector<FlowClassificationTable::Rule> rules;
    bool _children_merge;

};


class FlowContextDispatcher: public CTXDispatcher {

public:

    FlowContextDispatcher() CLICK_COLD {
        _children_merge = true;
    };
    ~FlowContextDispatcher() CLICK_COLD {};

    const char *class_name() const      { return "FlowContextDispatcher"; }
    const char *port_count() const      { return "1/-"; }
    const char *processing() const      { return PUSH; }


};



CLICK_ENDDECLS
#endif
