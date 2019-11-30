#include "xdptodevice.hh"
#include <click/args.hh>
#include <click/xdpsock.hh>

#include <unordered_set>

using std::string;
using std::unordered_set;

CLICK_DECLS

int XDPToDevice::initialize(ErrorHandler *errh) 
{

  _xfx = XDPManager::ensure(_dev, _prog, _xdp_flags, _bind_flags);
  return INITIALIZE_SUCCESS;

}

int XDPToDevice::configure(Vector<String> &conf, ErrorHandler *errh) 
{

  String dev,
         mode,
         prog;

  if(Args(conf, this, errh)
      .read_mp("DEV", dev)
      .read_or_set("PROG", prog, "xdpallrx")
      .read_or_set("MODE", mode, "skb")
      .read_or_set("TRACE", _trace, false)
      .consume() < 0 ) {

    return CONFIGURE_FAIL;
  
  }

  _dev = string{dev.c_str()};
  _prog = "/usr/lib/click/" + string{prog.c_str()} + ".o";
  return handle_mode(string{mode.c_str()}, errh);

}

void XDPToDevice::push(int port, Packet *p)
{
  u32 q = p->anno_u32(7);
  _xfx->tx(p, q);
  p->kill();
  _xfx->kick(q);
  if (_trace) {
    printf("[%s] tx q=%d\n", name().c_str(), q);
  }
}

void XDPToDevice::push_batch(int, PacketBatch *head)
{
  return;

  unordered_set<u32> to_kick{};

  for(Packet *p = head; p != nullptr; p = p->next()) {
    u32 q = p->anno_u32(7);
    _xfx->socks()[q]->tx(p);
    p->kill();
    to_kick.insert(q);
  }

  for (u32 q : to_kick) {
    _xfx->socks()[q]->kick();
  }

}

CLICK_ENDDECLS

EXPORT_ELEMENT(XDPToDevice)
