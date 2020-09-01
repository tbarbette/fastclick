// -*- c-basic-offset: 4; related-file-name: "../include/click/flow/flowelement.hh" -*-
/*
 * flowelement.{cc,hh} -- the FlowElement base class
 * Tom Barbette
 *
 * Copyright (c) 2015 University of Liege
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software")
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
#include <click/hashtable.hh>
#include <click/flow/flowelement.hh>
#include <algorithm>
#include <set>


CLICK_DECLS

#ifdef HAVE_FLOW

FlowElement::FlowElement()
{
    if (flow_code() != Element::COMPLETE_FLOW) {
        click_chatter("Flow Elements must be x/x in their flows");
        assert(flow_code() == Element::COMPLETE_FLOW);
    }
    if (in_batch_mode < BATCH_MODE_NEEDED)
        in_batch_mode = BATCH_MODE_NEEDED;
};

FlowElement::~FlowElement()
{
};
/*
FlowNode*
FlowElement::get_table(int iport, Vector<FlowElement*> contextStack) {

    return FlowElementVisitor::get_downward_table(this,-1,contextStack);
}

FlowNode*
FlowElement::resolveContext(FlowType, Vector<FlowElement*> contextStack) {
    return FlowClassificationTable::make_drop_rule().root;
}
*/

FlowType  FlowElement::getContext()
{
    return FLOW_NONE;
}

void *
VirtualFlowSpaceElement::cast(const char *name)
{
    if (strcmp("VirtualFlowSpaceElement", name) == 0) {
        return this;
    }
    return FlowElement::cast(name);
}

#include <click/config.h>
#include <click/flow/flowelement.hh>
#include <click/vector.hh>

/**
 * Give the distance between two elements that can cast to a type
 */
class ElementDistanceCastTracker : public RouterVisitor {
    public:
        typedef Pair<Element*,int> EDPair;

        ElementDistanceCastTracker(Router *router, bool stopAtFirst = true);

        /** @brief Return the elements that matched. */
        const Vector<EDPair> &elements() const {
            return _elements;
        }

        /** @brief Add element @a e to the set of matching elements. */
        void insert(Element *e, int distance);

        /** @brief Clear the set of matching elements. */
        void clear() {
            _reached.clear();
            _elements.clear();
        }

   private:
        bool visit(Element *e, bool isoutput, int port,
            Element *from_e, int from_port, int distance);

        int distance(Element *e, Element *from_e) {
            if (from_e && from_e->cast("VirtualFlowSpaceElement")) {
                //click_chatter("Distance to VFSE %p{element}: %d",e, distance);
                return dynamic_cast<VirtualFlowSpaceElement*>(from_e)->flow_data_size();
            }
            return 0;
        }

        Bitvector _reached;
        Vector<EDPair> _elements;
        bool _continue;
};

ElementDistanceCastTracker::ElementDistanceCastTracker(Router *router, bool stopAtFirst)
    :  _reached(router->nelements(), false), _continue(!stopAtFirst) {
}

void
ElementDistanceCastTracker::insert(Element *e, int distance)
{
    if (!_reached[e->eindex()]) {
        _reached[e->eindex()] = true;
        _elements.push_back(EDPair(e,distance));
    } else {
        for (int i = 0; i < _elements.size(); i++)
            if (_elements[i].first == e) {
                 if (_elements[i].second < distance)
                     _elements[i].second = distance;
                 break;
             }
     }
 }

bool
ElementDistanceCastTracker::visit(Element *e, bool, int, Element *, int, int distance)
{
    FlowElement* fe = dynamic_cast<FlowElement*>(e);
    if (fe && fe->stopClassifier())
        return false;
    if (e->cast("VirtualFlowSpaceElement")) {
        if (dynamic_cast<VirtualFlowSpaceElement*>(fe)->flow_data_size() > 0)
            insert(e,distance);
        return _continue;
    } else
        return true;
    }

    struct el {
        int id;
        int count;
        int distance;
    };

bool cmp(el a, el b)
{
    return a.count > b.count || (a.count==b.count &&  a.distance < b.distance);
}

VirtualFlowManager::VirtualFlowManager()
{
    _fcb_builded_init_future.add();
}

void VirtualFlowManager::find_children(int verbose)
{
    Element* e = this;

    // Find list of reachable elements
    ElementDistanceCastTracker reachables(e->router(),false);
    e->router()->visit_paths(this, true, -1, &reachables);

    if (verbose > 1) {
        click_chatter("Reachable VirtualFlowSpaceElement element list :");
        for (int i = 0; i < reachables.elements().size(); i++) {
            click_chatter("Reachable from %p{element} : %p{element}, max offset %d",this,reachables.elements()[i].first,reachables.elements()[i].second);
        }
    }

    _reachable_list = reachables.elements();
    _entries.push_back(this);
}

void VirtualFlowManager::build_fcb()
{
    _build_fcb(1,true);
}

Vector<VirtualFlowManager*> VirtualFlowManager::_entries;
CounterInitFuture VirtualFlowManager::_fcb_builded_init_future("FCBBuilder", VirtualFlowManager::build_fcb);


class ManagerReachVisitor : public RouterVisitor { public:
    Element* _target;
    bool _reached;

    ManagerReachVisitor(Element* t) : _target(t), _reached(false) {

    }

    bool visit(Element *e, bool, int,
               Element *, int, int) {
        FlowElement* fe = dynamic_cast<FlowElement*>(e);
        if (fe && fe->stopClassifier())
            return false;
        if (e == _target) {
            _reached = true;
            return false;
        }
        return true;
    }

    bool reached() {
        return _reached;
    }
};

bool
element_can_reach(Router* router, Element* a, Element* b) {
    ManagerReachVisitor r(b);
    router->visit(a, true, -1, &r);
    if (!r.reached())
        router->visit(a, false, -1, &r);
    return r.reached();
}

/**
 * This function builds the layout of the FCB by going through the graph starting from each entry elements
 */
void VirtualFlowManager::_build_fcb(int verbose, bool _ordered) {
    typedef Pair<int,int> CountDistancePair;
    HashTable<int,CountDistancePair> common(CountDistancePair{0,INT_MAX});

    Element* e = _entries[0];
    Router* router = e->router();

    if (verbose > 1)
        click_chatter("Building FCBs");
    // Counting elements that appear multiple times and their maximal distance
    for (int i = 0; i < _entries.size(); i++) {
        VirtualFlowManager* fc = dynamic_cast<VirtualFlowManager*>(_entries[i]);

        for (int j = 0; j < _entries[i]->_reachable_list.size(); j++) {
            if (verbose > 1) click_chatter("%p{element} : %d", _entries[i]->_reachable_list[j].first, _entries[i]->_reachable_list[j].second);
            auto ptr = common.find_insert(_entries[i]->_reachable_list[j].first->eindex(),CountDistancePair(0,_entries[i]->_reachable_list[j].second));
            ptr->second.first++;
            if (ptr->second.second < _entries[i]->_reachable_list[j].second) {
                ptr->second.second = _entries[i]->_reachable_list[j].second;
            }
        }
    }

    // Placing elements in a vector to sort them
    Vector<el> elements;
    for (auto it = common.begin(); it != common.end(); it++) {
        elements.push_back(el{it->first,it->second.first, it->second.second});
    }

    // Sorting the element, so we place the most shared first, then the minimal distance first. With the current version of the algo, this is not needed anymore
    std::sort(elements.begin(), elements.end(),cmp);

    // We now place all elements
    std::set<int> already_placed;
    for (auto it = elements.begin(); it != elements.end(); it++) {
        VirtualFlowSpaceElement* e = dynamic_cast<VirtualFlowSpaceElement*>(router->element(it->id));
        if (verbose > 1)
            click_chatter("Placing %p{element} : in %d sets, distance %d", e, it->count, it->distance);
        int my_place;
        int min_place = 0;

        //We need to verify the reserved space for all possible FlowManager
        for (int i = 0; i < _entries.size(); i++) {
            VirtualFlowManager* fc = dynamic_cast<VirtualFlowManager*>(_entries[i]);
            //If this flow manager can reach the element, then we need to have enough reserved space
            for (int j = 0; j < _entries[i]->_reachable_list.size(); j++) {
                if (_entries[i]->_reachable_list[j].first->eindex() == it->id) {
                    if (fc->_reserve > min_place)
                        min_place = fc->_reserve;

                    break;
                }
            }
        }

        if (_ordered)
            my_place = min_place + it->distance;
        else
            my_place = min_place;
        Bitvector v(false);

        /**
        * THe followoing is for verification purpose
        */
        // For each already placed element that are reachable from this one, we set the assigned bits in the vector
        for (auto ai = already_placed.begin(); ai != already_placed.end(); ai++) {
            int aid = *ai;
            VirtualFlowSpaceElement* ae = dynamic_cast<VirtualFlowSpaceElement*>(router->element(aid));
            if (element_can_reach(router, e,ae)) {
                if (v.size() < ae->flow_data_offset() + ae->flow_data_size())
                    v.resize(ae->flow_data_offset() + ae->flow_data_size());
                v.set_range(ae->flow_data_offset(), ae->flow_data_size(), true);
                if (_ordered && !(ae->flow_data_offset() + ae->flow_data_size() <= my_place || ae->flow_data_offset() >= my_place + e->flow_data_size())) {
                    click_chatter("FATAL ERROR : Cannot place  %p{element} at [%d-%d] because it collides with %p{element}",e,my_place,my_place + e->flow_data_size() -1, ae);
                    assert(false);
                }
            }
        }

        while (!_ordered && v.range(my_place,e->flow_data_size())) {
            my_place++;
        }

        if (verbose > 0)
            click_chatter("Placing  %p{element} at [%d-%d]",e,my_place,my_place + e->flow_data_size() -1 );
        already_placed.insert(it->id);
        e->_flow_data_offset = my_place;
	_fcb_builded_init_future.post(e);
    }

    //Set pool data size for classifiers
    for (int i = 0; i < _entries.size(); i++) {
        VirtualFlowManager* fc = _entries[i];
//        fc->_reserve = min_place;
        for (int j = 0; j < fc->_reachable_list.size(); j++) {
            VirtualFlowSpaceElement* vfe = dynamic_cast<VirtualFlowSpaceElement*>(fc->_reachable_list[j].first);
            int tot = vfe->flow_data_offset() + vfe->flow_data_size();
            if (tot > fc->_reserve)
                fc->_reserve = tot;
        }
    }
}

#endif
CLICK_ENDDECLS
