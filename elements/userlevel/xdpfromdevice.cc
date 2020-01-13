#include "xdpfromdevice.hh"

#include <click/args.hh>
#include <click/xdpsock.hh>

using std::shared_ptr;
using std::string;
using std::vector;

CLICK_DECLS

int XDPFromDevice::initialize(ErrorHandler *errh) {

  _xfx = XDPManager::get().ensure(_dev, _prog, _xdp_flags, _bind_flags, _trace);
  _t = new Task(this);
  _t->initialize(this, true);

  return INITIALIZE_SUCCESS;

}

int XDPFromDevice::configure(Vector<String> &conf, ErrorHandler *errh) {

  String dev,
         mode,
         prog;
  bool   zero_copy;

  if(Args(conf, this, errh)
      .read_mp("DEV", dev)
      .read_or_set("MODE", mode, "skb")
      .read_or_set("ZEROCOPY", zero_copy, false)
      .read_or_set("PROG", prog, "xdpallrx")
      .read_or_set("TRACE", _trace, false)
      .consume() < 0 ) {

    return CONFIGURE_FAIL;
  
  }

  if (zero_copy) {
    _bind_flags |= XDP_ZEROCOPY;
  } else {
    _bind_flags |= XDP_COPY;
  }


  _dev = string{dev.c_str()};
  _prog = "/usr/lib/click/" + string{prog.c_str()} + ".o";
  return handle_mode(string{mode.c_str()}, errh);

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

  const vector<PBuf> &pbufs = _xfx->rx();

  for(size_t i=0; i<pbufs.size(); i++) {

    for(size_t j=0; j<pbufs[i].len; j++) {
      output(0).push(pbufs[i].pkts[j]);
      if (_trace) {
        printf("[%s] rx q=%d\n", name().c_str(), i);
      }
    }

  }

}
#endif


CLICK_ENDDECLS

EXPORT_ELEMENT(XDPFromDevice)
