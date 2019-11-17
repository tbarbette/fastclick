#pragma once

#include <memory>
#include <unordered_map>
#include "xdpsock.hh"

class XDPManager {

  public:
    static std::shared_ptr<XDPSock> get(std::string dev);
    static std::shared_ptr<XDPSock> ensure(std::string dev);

  private:
    static XDPManager& get();

    std::unordered_map<std::string, std::shared_ptr<XDPSock>> socks;

};
