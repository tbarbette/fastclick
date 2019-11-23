#include "xdpfromdevice.hh"

#include <click/args.hh>

using std::shared_ptr;
using std::string;
using std::vector;

CLICK_DECLS

int XDPFromDevice::initialize(ErrorHandler *errh) {

  _sock = XDPManager::ensure(_dev);
  _t = new Task(this);
  _t->initialize(this, true);

  return INITIALIZE_SUCCESS;

}

int XDPFromDevice::configure(Vector<String> &conf, ErrorHandler *errh) {

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

bool XDPFromDevice::run_task(Task *t)
{
  
  #if HAVE_BATCH
  rx_batch();
  #else
  rx();
  #endif

  t->fast_reschedule();

  return true;

}

#if HAVE_BATCH
void XDPFromDevice::rx_batch()
{

  vector<Packet*> pkts = _sock->rx();
  if (pkts.empty()) {
    return;
  }

  PacketBatch *head = PacketBatch::start_head(pkts[0]);
  for(size_t i=0; i<pkts.size()-1; i++) {
    pkts[i]->set_next(pkts[i+1]);    
  }
  head->make_tail(*pkts.end(), pkts.size());

  output_push_batch(0, head);

}
#else
void XDPFromDevice::rx()
{
  vector<Packet*> pkts = _sock->rx();
  for(Packet *p : pkts) {
    output(0).push(p);
  }
}
#endif


CLICK_ENDDECLS

EXPORT_ELEMENT(XDPFromDevice)
