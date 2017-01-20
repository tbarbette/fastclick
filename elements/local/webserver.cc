/*
 * TCPReflector.{cc,hh} -- toy TCP server
 * Robert Morris
 *
 * Copyright (c) 1999-2001 Massachusetts Institute of Technology
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

#include <click/config.h>
#include "webserver.hh"
#include <clicknet/tcp.h>
#include <clicknet/ip.h>
#include <click/ipaddress.hh>
#include <click/glue.hh>
#include <click/args.hh>
CLICK_DECLS

WebServer::WebServer()
{
}

WebServer::~WebServer()
{
}

int
WebServer::configure (Vector<String> &conf, ErrorHandler *errh)
{
    int ret;
    int len = -1;
    String body = "random bullshit in a page";
    String meta;
    ret = Args(conf, this, errh)
            .read("META", meta)
            .read("BODY", body)
            .read("LENGTH", len)
            .complete();
    String begin="<html><head><title>Click HTTP Server</title>"+meta+"<body>";
    String end = "</body></html>";
    while (begin.length() + end.length() < len) {
      begin += body;
    }
  String page = begin + end;
  _data =  "HTTP/1.1 200 OK\n\r"
           "Server: Click/1.0.0\n\r"
           "Content-Length: "+String(page.length())+"\n\r"
           "Content-Type: text/html\n\r"
           "Connection: Closed;\n\r"
           "\n\r"
          +page;
    return ret;
}
CLICK_ENDDECLS
EXPORT_ELEMENT(WebServer)