#pragma once

#include <unordered_map>
#include <click/xdpinterface.hh>

class XDPManager {

  public:
    static XDPInterfaceSP get(string dev);
    static XDPInterfaceSP ensure(
        string dev, string prog, u16 xdp_flags, u16 bind_flags
    );

  private:
    static XDPManager& get();
    static XDPInterfaceSP create_device_sockets(
        string dev, string prog, u16 xdp_flags, u16 bind_flags
    );

    std::unordered_map<string, XDPInterfaceSP> ifxs;

};
