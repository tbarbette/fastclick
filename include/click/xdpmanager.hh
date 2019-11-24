#pragma once

#include <memory>
#include <unordered_map>
#include "xdpsock.hh"

class XDPManager {

  public:
    static std::shared_ptr<XDPSock> get(std::string dev);
    static std::shared_ptr<XDPSock> ensure(
        std::string dev, u16 xdp_flags, u16 bind_flags, u32 queue_id);

  private:
    static XDPManager& get();

    std::unordered_map<std::string, std::shared_ptr<XDPSock>> socks;

};
