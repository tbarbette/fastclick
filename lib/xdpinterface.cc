/*
 * xdpinterface.{cc,hh} Manage access to express data path (XDP) sockets
 */

#include <click/xdpinterface.hh>
#include <click/xdpsock.hh>

extern "C" {
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <sys/ioctl.h>
#include <poll.h>
}

#include <stdexcept>

using std::map;
using std::make_shared;
using std::runtime_error;

#define MAX_ERRNO	4095
#define IS_ERR_VALUE(x) unlikely((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)
inline bool IS_ERR_OR_NULL(const void *ptr)
{
	return unlikely(!ptr) || IS_ERR_VALUE((unsigned long)ptr);
}

XDPInterface::XDPInterface(
    string dev,
    string prog,
    u16 xdp_flags,
    u16 bind_flags
) 
: _dev{dev},
  _prog{prog},
  _xdp_flags{xdp_flags},
  _bind_flags{bind_flags}
{ }

void XDPInterface::init()
{
  _ifindex = if_nametoindex(_dev.c_str());
  if (!_ifindex) {
    die("interface does not exist");
  }

  load_bpf();
  create_device_sockets();
}

void XDPInterface::load_bpf()
{
  load_bpf_program();
  load_bpf_maps();
}

void XDPInterface::load_bpf_program()
{
  printf("loading program %s\n", _prog.c_str());
  struct bpf_prog_load_attr pla = {
    .file = _prog.c_str(),
    .prog_type = BPF_PROG_TYPE_XDP,
  };

  int err = bpf_prog_load_xattr(&pla, &_bpf_obj, &_bpf_fd);
  if (err) {
    die("failed to load bpf program", err);
  }
  if (_bpf_fd < 0) {
    die("failed to load bpf program (fd)");
  }

  printf("applying program to index %d\n", _ifindex);
  // apply the bpf program to the specified link
  err = bpf_set_link_xdp_fd(_ifindex, _bpf_fd, _xdp_flags);
  if(err < 0) {
    die("xdp link set failed", err);
  }
}

void XDPInterface::load_bpf_maps()
{

  // load socket map
  _xsks_map = bpf_object__find_map_by_name(_bpf_obj, "xsks_map");
  if (IS_ERR_OR_NULL(_xsks_map))
    die("could not find xsks_map");

  _xsks_map_fd = bpf_map__fd(_xsks_map);
  if (_xsks_map_fd < 0) {
    die("failed to load xsks_map", _xsks_map_fd);
  }

}

void XDPInterface::create_device_sockets()
{

  // determine the number of channels the interface has via ethtool

  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    throw runtime_error{"failed to create ethtool socket"};
  }

  ethtool_channels channels; 
  channels.cmd = ETHTOOL_GCHANNELS;

  ifreq ifr;
  ifr.ifr_data = reinterpret_cast<char*>(&channels);
  memcpy(ifr.ifr_name, _dev.c_str(), _dev.size());

  int err = ioctl(fd, SIOCETHTOOL, &ifr);
  if (err && errno != EOPNOTSUPP) {
    close(fd);
    printf("ethtool ioctl err %s\n", strerror(err));
    throw runtime_error{"ethtool ioctl failed"};
  }

  int numchan{0};
  if (err || channels.max_combined == 0) {
    numchan = 1;
  }
  else {
    numchan = channels.max_combined;
  }

  // create an XDP socket per interface channel
  
  printf("initializing %d queues for %s\n", numchan, _dev.c_str());

  for (u32 i=0; i<numchan; i++) {
    _socks.push_back(
        make_shared<XDPSock>(shared_from_this(), i)
    );
  }

  // create polling file descriptors
  
  for(XDPSockSP x : _socks)
  {
    _poll_fds.push_back(x->poll_fd());
  }

}

XDPPacketMap XDPInterface::rx()
{
  XDPPacketMap xpm;

  // poll the file descriptors of all the XDP sockets associated with this
  // interface
  int ret = poll(_poll_fds.data(), _poll_fds.size(), 0);
  if (ret <= 0) {
    return xpm;
  }

  // for any sockets that registered a POLLIN event, collect the packets from
  // that sockets ring buffers
  for(u32 i=0; i<_poll_fds.size(); i++)
  {
    if(_poll_fds[i].revents & POLLIN)
    {
      xpm[i] = _socks[i]->rx();
    }
  }

  return xpm;
}

void XDPInterface::tx(Packet *p, u32 queue_id)
{
  _socks[queue_id]->tx(p);
}

void XDPInterface::kick(u32 queue_id)
{
  _socks[queue_id]->kick();
}
