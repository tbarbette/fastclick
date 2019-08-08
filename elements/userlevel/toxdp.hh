#pragma once
#include <click/config.h>
#include <click/batchelement.hh>
#include <click/xdp_common.hh>

CLICK_DECLS

class ToXDP : public BaseXDP {

  public:

    ToXDP() CLICK_COLD = default;
    ~ToXDP() CLICK_COLD = default;

    const char *class_name() const override final { return "ToXDP"; }
    const char *port_count() const override final { return "1/0"; }
    const char *processing() const override final { return PUSH; }

    void push(int port, Packet *p) override final;

};

CLICK_ENDDECLS
