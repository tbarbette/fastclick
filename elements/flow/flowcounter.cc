/*
 * flowcounter.{cc,hh} -- remove insults in web pages
 * Tom Barbette
 */

#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include "flowcounter.hh"

CLICK_DECLS

FlowCounter::FlowCounter() : StatVector(Vector<int>(32768,0))
{

}

int FlowCounter::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if(Args(conf, this, errh)
    .complete() < 0)
        return -1;

    return 0;
}

void FlowCounter::push_flow(int, int* fcb, PacketBatch* flow)
{
    *fcb += flow->count();
    output_push_batch(0, flow);
}


enum { h_count, h_open };

String
FlowCounter::read_handler(Element *e, void *thunk)
{
    FlowCounter *fd = static_cast<FlowCounter *>(e);
    switch ((intptr_t)thunk) {
      case h_count: {
          PER_THREAD_MEMBER_SUM(uint64_t,count, fd->_state, count);
          return String(count);
                    }
       case h_open: {
          PER_THREAD_MEMBER_SUM(uint64_t,open, fd->_state, open);
          return String(open);
            }
      default:
          return "<error>";
    }
}

int
FlowCounter::write_handler(const String &s_in, Element *e, void *thunk, ErrorHandler *errh)
{
    return -1;
}

void
FlowCounter::add_handlers() {
    add_read_handler("count", read_handler, h_count);
    add_read_handler("open", read_handler, h_open);
    add_stat_handler(this);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(FlowCounter)
ELEMENT_MT_SAFE(FlowCounter)
