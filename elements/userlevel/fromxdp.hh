#pragma once
#include <click/xdp_common.hh>

CLICK_DECLS

class FromXDP : public BaseXDP {

  public:

    FromXDP() CLICK_COLD = default;
    ~FromXDP() CLICK_COLD = default;

    const char *class_name() const override final { return "FromXDP"; }
    const char *port_count() const override final { return "0/1"; }
    const char *processing() const override final { return PUSH; }

    bool run_task(Task *t) override final;

};

CLICK_ENDDECLS
