#ifndef CLICK_WEBSERVER_HH
#define CLICK_WEBSERVER_HH
#include <click/element.hh>
#include <click/glue.hh>
#include <click/timer.hh>
#include <click/ipaddress.hh>
#include "../tcpudp/tcpreflector.hh"
CLICK_DECLS

/*
 * =c
 * WebServer()
 */

class WebServer : public TCPReflector {
 public:

    WebServer() CLICK_COLD;
  ~WebServer() CLICK_COLD;

  const char *class_name() const override		{ return "WebServer"; }
  const char *port_count() const override		{ return PORTS_1_1; }

    int configure(Vector<String> &conf, ErrorHandler *errh) CLICK_COLD;

};

CLICK_ENDDECLS
#endif
