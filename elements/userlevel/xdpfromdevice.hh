#pragma once

#include <click/config.h>
#include <click/element.hh>
#include <click/xdpmanager.hh>

CLICK_DECLS

class XDPFromDevice : public Element {

  public:

    XDPFromDevice() CLICK_COLD = default;
    ~XDPFromDevice() CLICK_COLD = default;

    const char *class_name() const override final { return "XDPFromDevice"; }
    const char *port_count() const override final { return PORTS_0_1; }
    const char *processing() const override final { return PUSH; }

};

CLICK_ENDDECLS
