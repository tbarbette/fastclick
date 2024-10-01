#include <click/config.h>
#include "checkbierheader.hh"
#include <click/element.hh>
#include <clicknet/ip6.h>
#include <click/packet.hh>
#include <clicknet/bier.h>
#include <click/args.hh>
#include <click/error.hh>
#include <click/standard/alignmentinfo.hh>

CLICK_DECLS

CheckBIERHeader::CheckBIERHeader() : _offset(0) {
  _drops = 0;
}

CheckBIERHeader::~CheckBIERHeader() {}

int CheckBIERHeader::configure(Vector<String> &conf,ErrorHandler *errh) {
  if (Args(conf, this, errh)
      .read_p("OFFSET", _offset)
      .complete() < 0
  )
    return -1;
  return 0;
}

void CheckBIERHeader::drop(Packet *p) {
  if (_drops == 0) {
    click_chatter("BIER header check failed");
  }
  _drops++;

  if (noutputs() == 2) {
    output(1).push(p);
  } else {
    p->kill();
  }
  
}

Packet *CheckBIERHeader::simple_action(Packet *p) {
  const click_bier *bier;
  unsigned plen;
  // const click_ip6 *iph = (click_ip6*) p->ip_header();
  // if (iph->ip6_nxt != IP6PROTO_BIERIN6) {
    // click_chatter("Not a BIERin6 packet");
    // goto drop;
  // }

  bier = reinterpret_cast<const click_bier*>(p->data());
  plen = p->length();

  if((int)plen < (int)sizeof(click_bier)) {
    click_chatter("BIERin6 header too small");
    goto drop;
  }
  
  return(p);

  drop:
    drop(p);
    return 0;
}

String CheckBIERHeader::read_handler(Element *e, void *thunk) {
  CheckBIERHeader *c = reinterpret_cast<CheckBIERHeader *>(e);
  switch (reinterpret_cast<uintptr_t>(thunk)) {
    case h_drops: {
        return String(c->_drops);
    }
    defaut: {
      return String();
    }
  }
}

void CheckBIERHeader::add_handlers() {
  add_read_handler("drops", read_handler, h_drops);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(CheckBIERHeader)
ELEMENT_MT_SAFE(CheckBIERHeader)
