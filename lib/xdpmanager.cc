/*
 * xdpmanager.{cc,hh} Manage access to express data path (XDP) sockets
 */

#include <click/xdpmanager.hh>
#include <click/xdpumem.hh>

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

XDPManager::XDPManager()
{
  _xm = make_shared<XDPUMEM>(NUM_FRAMES, FRAME_SIZE, NUM_RX_DESCS, NUM_TX_DESCS);
}

XDPInterfaceSP XDPManager::ensure(
    string dev, string prog, u16 xdp_flags, u16 bind_flags, bool trace
)
{
  auto &ifxs = get().ifxs;
  auto x = ifxs.find(dev);
  if (x == ifxs.end()) {

    printf("creating new xdp socket for %s\n", dev.c_str());
    XDPInterfaceSP xfx = make_shared<XDPInterface>(
        dev, prog, xdp_flags, bind_flags, _xm, trace
    );
    xfx->init();

    ifxs[dev] = xfx;

  }

  return ifxs[dev];
}

