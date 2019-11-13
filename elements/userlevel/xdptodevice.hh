#pragma once

#include <click/config.h>
#include <click/element.hh>
#include <click/xdpmanager.hh>

CLICK_DECLS

class XDPToDevice : public Element {

  public:

    XDPToDevice() CLICK_COLD = default;
    ~XDPToDevice() CLICK_COLD = default;

    const char *class_name() const override final { return "XDPToDevice"; }
    const char *port_count() const override final { return PORTS_1_0; }
    const char *processing() const override final { return PUSH; }

};

CLICK_ENDDECLS
