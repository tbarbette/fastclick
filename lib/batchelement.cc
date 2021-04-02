// -*- c-basic-offset: 4; related-file-name: "../include/click/batchelement.hh" -*-
/*
 * batchelement.{cc,hh}
 *
 * SubClass of Element used for elements supporting batching.
 */
#include <click/config.h>
#include <click/glue.hh>
#include <click/batchelement.hh>
#include <click/routervisitor.hh>

CLICK_DECLS

#ifdef HAVE_BATCH

BatchElement::BatchElement()
{
    in_batch_mode = Element::BATCH_MODE_IFPOSSIBLE;
}

BatchElement::~BatchElement()
{
}

bool BatchElement::BatchModePropagate::visit(Element *e, bool, int, Element *from, int from_port, int)
{
    // Do not continue if we change from pull to push
    if ((ispush && !from->output_is_push(from_port)) || (!ispush && !from->input_is_pull(from_port))) return false;

    if (e->in_batch_mode > Element::BATCH_MODE_NO) {
        e->in_batch_mode = Element::BATCH_MODE_YES;
        e->receives_batch = true;
#if BATCH_DEBUG
        click_chatter("%s is now in batch mode", e->name().c_str());
#endif
        return true;
    }

#if HAVE_VERBOSE_BATCH
        if (ispush)
            click_chatter("Warning! Push %s->%s is not compatible with batch. "
                    "Packets will be unbatched and that will reduce the performance.",
                    from->name().c_str(), e->name().c_str());
        else
            click_chatter("Warning! Pull %s<-%s is not compatible with batch. "
                    "Batching will be disabled and that will reduce the performance.",
                            e->name().c_str(), from->name().c_str());
#endif

    /*If this is push, the start_batch/end_batch message will be passed up to
        the downwards batch elements, we have to propagate until them*/
    if (ispush) {
#if BATCH_DEBUG
        click_chatter("Starting bridge traversal at %p{element}->%p{element}",from,e);
#endif
        PushToPushBatchVisitor v(e);
        e->router()->visit(e,1,-1,&v);
    }
    return false;
}

/**
 * RouterVisitor finding all reachable batch-enabled element. Used to re-batch before those
 */
BatchElement::PushToPushBatchVisitor::PushToPushBatchVisitor(Element* origin) :_origin(origin)
{

}

bool
BatchElement::PushToPushBatchVisitor::visit(Element *e, bool, int, Element *, int, int)
{
    if (e->in_batch_mode == BATCH_MODE_IFPOSSIBLE) {
        e->in_batch_mode = BATCH_MODE_YES;
#if BATCH_DEBUG
        click_chatter("%s is now in batch mode", e->name().c_str());
#endif
#if HAVE_AUTO_BATCH == AUTO_BATCH_LIST
        list->append(e);
#endif
        e->receives_batch = true;
        return false;
    };
    return true;
}

#endif

CLICK_ENDDECLS
