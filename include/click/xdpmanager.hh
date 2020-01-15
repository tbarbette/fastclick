#pragma once

#include <unordered_map>
#include <click/xdpinterface.hh>

class XDPManager {

  public:
    static XDPManager& get();
    static XDPInterfaceSP get(string dev);
    XDPInterfaceSP ensure(
        string dev, string prog, u16 xdp_flags, u16 bind_flags, bool trace
    );

  private:
    XDPManager();

    std::unordered_map<string, XDPInterfaceSP> ifxs;

    //XDPUMEMSP _xm{nullptr};
    // packet buffer
    void *_pbuf;

};
