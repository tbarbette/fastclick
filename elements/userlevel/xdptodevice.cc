#include "xdptodevice.hh"
#include <click/args.hh>

using std::string;

CLICK_DECLS

int XDPToDevice::initialize(ErrorHandler *errh) 
{

  _sock = XDPManager::ensure(_dev);
  return INITIALIZE_SUCCESS;

}

int XDPToDevice::configure(Vector<String> &conf, ErrorHandler *errh) 
{

  String dev,
         mode,
         prog;

  if(Args(conf, this, errh)
      .read_mp("DEV", dev)
      .read_or_set("MODE", mode, "skb")
      .read_or_set("PROG", prog, "xdpallrx")
      .read_or_set("TRACE", _trace, false)
      .consume() < 0 ) {

    return CONFIGURE_FAIL;
  
  }

  _dev = string{dev.c_str()};
  _mode = string{mode.c_str()};
  _prog = string{prog.c_str()};

  return CONFIGURE_SUCCESS;

}

void XDPToDevice::push(int port, Packet *p)
{
  _sock->tx(p);
  p->kill();
  _sock->kick();
}

void XDPToDevice::push_batch(int, PacketBatch *head)
{

  for(Packet *p = head; p != nullptr; p = p->next()) {
    _sock->tx(p);
    p->kill();
  }

  _sock->kick();

}

CLICK_ENDDECLS

EXPORT_ELEMENT(XDPToDevice)
