#pragma once

#include <click/config.h>
#include <click/element.hh>
#include <click/xdpmanager.hh>
#include <click/task.hh>

CLICK_DECLS

class XDPFromDevice : public Element {

  public:

    XDPFromDevice() CLICK_COLD = default;
    ~XDPFromDevice() CLICK_COLD = default;

    int configure(Vector<String>&, ErrorHandler*) override CLICK_COLD;
    int initialize(ErrorHandler*) override CLICK_COLD;

    const char *class_name() const override final { return "XDPFromDevice"; }
    const char *port_count() const override final { return PORTS_0_1; }
    const char *processing() const override final { return PUSH; }

    bool run_task(Task *t) override final;

  private:
    Task *_t{nullptr};

    std::string _dev,
                _mode,
                _prog;

    bool _trace{false};

    std::shared_ptr<XDPSock> _sock{nullptr};

};

CLICK_ENDDECLS
