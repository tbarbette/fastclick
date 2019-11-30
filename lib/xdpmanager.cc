/*
 * xdpmanager.{cc,hh} Manage access to express data path (XDP) sockets
 */

#include <click/xdpmanager.hh>

using std::make_shared;

XDPManager& XDPManager::get()
{
  static XDPManager xm;
  return xm;
}

XDPInterfaceSP XDPManager::get(string dev) 
{
  auto &ifxs = get().ifxs;
  auto x = ifxs.find(dev);
  if (x == ifxs.end()) {
    return nullptr;
  }

  return x->second;
}

XDPInterfaceSP XDPManager::ensure(
    string dev, string prog, u16 xdp_flags, u16 bind_flags
)
{
  auto &ifxs = get().ifxs;
  auto x = ifxs.find(dev);
  if (x == ifxs.end()) {
    printf("creating new xdp socket for %s\n", dev.c_str());
    ifxs[dev] = make_shared<XDPInterface>(dev, prog, xdp_flags, bind_flags);
    ifxs[dev]->init();
  }

  return ifxs[dev];
}

