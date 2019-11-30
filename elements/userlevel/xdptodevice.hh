#pragma once

#include <click/config.h>
#include <click/batchelement.hh>
#include <click/xdpmanager.hh>
#include "xdpdev.hh"

CLICK_DECLS

class XDPToDevice : public BatchElement, XDPDev {

  public:

    XDPToDevice() CLICK_COLD = default;
    ~XDPToDevice() CLICK_COLD = default;

    int configure(Vector<String>&, ErrorHandler*) override CLICK_COLD;
    int initialize(ErrorHandler*) override CLICK_COLD;

    const char *class_name() const override final { return "XDPToDevice"; }
    const char *port_count() const override final { return PORTS_1_0; }
    const char *processing() const override final { return PUSH; }

    void push(int port, Packet *p) override final;
    void push_batch(int, PacketBatch *head);

  private:


    bool _trace{false};

    std::shared_ptr<XDPInterface> _xfx{};

};

CLICK_ENDDECLS
