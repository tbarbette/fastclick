#pragma once

#include <click/config.h>
#include <click/batchelement.hh>
#include <click/xdpmanager.hh>

CLICK_DECLS

class XDPToDevice : public BatchElement {

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

    std::string _dev,
                _mode,
                _prog;

    bool _trace{false};

    std::shared_ptr<XDPSock> _sock{nullptr};

};

CLICK_ENDDECLS
