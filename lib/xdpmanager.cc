/*
 * xdpmanager.{cc,hh} Manage access to express data path (XDP) sockets
 */

#include <click/xdpmanager.hh>

using std::shared_ptr;
using std::make_shared;
using std::unordered_map;
using std::string;

XDPManager& XDPManager::get()
{
  static XDPManager xm;
  return xm;
}

shared_ptr<XDPSock> XDPManager::get(string dev) 
{
  auto &socks = get().socks;
  auto x = socks.find(dev);
  if (x == socks.end()) {
    return nullptr;
  }

  return x->second;
}

shared_ptr<XDPSock> XDPManager::ensure(
    string dev, u16 xdp_flags, u16 bind_flags, u32 queue_id
)
{
  auto &socks = get().socks;
  auto x = socks.find(dev);
  if (x == socks.end()) {
    socks[dev] = make_shared<XDPSock>(dev, xdp_flags, bind_flags, queue_id);
  }

  return socks[dev];
}
