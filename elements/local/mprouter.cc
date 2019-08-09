/*
 * mprouter.{cc,hh} -- A round-robin multipath router.
 *
 * Ryan Goodfellow
 *
 * Copyright (c) 2019 mergetb.org
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
#include "mprouter.hh"
#include <click/args.hh>
#include <click/error.hh>

using std::map;

CLICK_DECLS

// lifetime management --------------------------------------------------------

int MultipathRouter::configure(Vector<String> &conf, ErrorHandler *errh) {

  for(size_t i=0; i<conf.size(); i++) {

    IPRoute rt;
    bool ok = cp_ip_route(conf[i], &rt, false, this);
    if (!ok) {
      errh->error("argument %zu should be `ADDR/MASK [GW] PORT`", i);
      return CONFIGURE_FAIL;
    }

    if (rt.port < 0 || rt.port > noutputs()) {
      errh->error("output %zu is not a thing", i);
      return CONFIGURE_FAIL;
    }

    add_route(rt, errh);

  }

  return CONFIGURE_SUCCESS;
}

// packet processing ----------------------------------------------------------
 
void MultipathRouter::push(int port, Packet *p) {

  const click_ip *ip = reinterpret_cast<const click_ip*>(p->data()+14);
  uint32_t dst = ip->ip_dst.s_addr;

  uint32_t pfx = dst;

  auto iter = _t32.find(pfx);
  if(iter != _t32.end()) {
    take_path(port, p, iter->second);
    return;
  }

  pfx = dst & _p24;
  iter = _t24.find(pfx);
  if(iter != _t24.end()) {
    take_path(port, p, iter->second);
    return;
  }

  pfx = dst & _p16;
  iter = _t16.find(pfx);
  if(iter != _t16.end()) {
    take_path(port, p, iter->second);
    return;
  }

  pfx = dst & _p8;
  iter = _t8.find(pfx);
  if(iter != _t8.end()) {
    take_path(port, p, iter->second);
    return;
  }

  if(_default) {
    take_path(port, p, *_default);
    return;
  }

  //printf("%s wayward llama (%x) %x\n", name().c_str(), dst & _p24, dst);

}

void MultipathRouter::take_path(int port, Packet *x, Path &p) {

    //printf("%s ---> [%d]\n", name().c_str(), port);

    // select output path based on current robin
    output(p.ports[p.robin]).push(x);

    // round the robin
    p.robin = (p.robin + 1) % p.ports.size();

}

// metadata -------------------------------------------------------------------

//const char* MultipathRouter::class_name() const { return "MultipathRouter"; }
const char* MultipathRouter::processing() const { return PUSH; }
const char* MultipathRouter::port_count() const { return "-/-"; }

// route management -----------------------------------------------------------

bool MultipathRouter::add_route(const IPRoute &rt, ErrorHandler *errh) {

  uint32_t len = rt.prefix_len();
  uint32_t pfx = rt.addr.addr() & IPAddress::make_prefix(len).addr();
    
  printf("%s added route %u %x %u\n", name().c_str(), len, pfx, rt.port);


  switch(len) {
    case 32:
      add_route_path(_t32, pfx, rt.port);
      return true;
    case 24:
      add_route_path(_t24, pfx, rt.port);
      return true;
    case 16:
      add_route_path(_t16, pfx, rt.port);
      return true;
    case 8:
      add_route_path(_t8, pfx, rt.port);
      return true;
    case 0:
      add_default_path(rt.port);
      return true;
    default:
      errh->error("invalid prefix %u length, must be one of 32,24,16,8,0", len);
      return false;
  }

}

void MultipathRouter::add_route_path(
    map<uint32_t,Path> &m, uint32_t pfx, uint32_t port) {

  auto iter = m.find(pfx);
  if(iter == m.end()) {
    Path p;
    p.ports.push_back(port);

    m[pfx] = p;
    return;
  } 

  iter->second.ports.push_back(port);

}

void MultipathRouter::add_default_path(uint32_t port) {

  if (!_default) { _default = new Path(); }

  _default->ports.push_back(port);

}

CLICK_ENDDECLS
EXPORT_ELEMENT(MultipathRouter)
