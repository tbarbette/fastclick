#pragma once

#include <memory>
#include <unordered_map>
#include "xdpsock.hh"

class XDPManager {

  public:
    std::shared_ptr<XDPSock> get(std::string dev);

  private:
    std::unordered_map<std::string, std::shared_ptr<XDPSock>> socks;

};
