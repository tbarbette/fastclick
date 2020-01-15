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
    //_xm = make_shared<XDPUMEM>(NUM_FRAMES, FRAME_SIZE, NUM_RX_DESCS, NUM_TX_DESCS);
    _pbuf = mmap(
        NULL, 
        NUM_FRAMES * FRAME_SIZE, 
        PROT_READ | PROT_WRITE, 
        MAP_PRIVATE | MAP_ANONYMOUS, 
        -1, 
        0
    );

    if (_pbuf == MAP_FAILED) {
        printf("ERROR: mmap failed\n");
        exit(EXIT_FAILURE);
    }
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
        dev, prog, xdp_flags, bind_flags, _pbuf, trace
    );
    xfx->init();

    ifxs[dev] = xfx;

  }

  return ifxs[dev];
}

