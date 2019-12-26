/*
 * xdpsock.{cc,hh} An express data path socket (XDP) object
 */

#include <click/xdpsock.hh>
#include <click/xdpinterface.hh>
#include <poll.h>

using std::make_shared;
using std::string;
using std::vector;

XDPSock::XDPSock(XDPInterfaceSP xfx, u32 queue_id, bool trace)
: _xfx{xfx},
  _queue_id{queue_id},
  _trace{trace}
{
  configure_umem();
  configure_socket();
  printf("xdpsock %s is ready\n", xfx->dev().c_str());
}

void XDPSock::configure_umem()
{

  struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
  if (setrlimit(RLIMIT_MEMLOCK, &r)) {
    die("failed to set rlimit", errno);
  }

  int ret = posix_memalign(&_umem_buf, getpagesize(), NUM_FRAMES * FRAME_SIZE);
  if (ret) {
    die("failed to align umem buffer", ret);
  }

  if(_trace) {
    printf("fill size:  %d\n", NUM_RX_DESCS);
    printf("comp size:  %d\n", NUM_TX_DESCS);
    printf("frame size: %d\n", FRAME_SIZE);
    printf("num frames: %d\n", NUM_FRAMES);
  }

  xsk_umem_config cfg = {
		.fill_size = NUM_RX_DESCS,
		.comp_size = NUM_TX_DESCS,
		.frame_size = FRAME_SIZE,
		.frame_headroom = XSK_UMEM__DEFAULT_FRAME_HEADROOM,
	};

  _umem = static_cast<xsk_umem_info*>( calloc(1, sizeof(xsk_umem_info)) );
  if (_umem == nullptr) {
    die("failed to allocate umem", errno);
  }

  ret = xsk_umem__create(
    &_umem->umem,
    _umem_buf,
    NUM_FRAMES * FRAME_SIZE,
    &_umem->fq,
    &_umem->cq,
    &cfg
  );
  if (ret) {
    die("failed to create umem", ret);
  }
  _umem->buffer = _umem_buf;

  _umem_mgr = make_shared<XDPUMEM>(NUM_FRAMES);
}

void XDPSock::configure_socket()
{

  _xsk = static_cast<xsk_socket_info*>( calloc(1, sizeof(xsk_socket_info)) );
  if (_xsk == nullptr) {
    die("failed to allocate xsk", errno);
  }
  _xsk->umem = _umem;

  xsk_socket_config cfg{
    .rx_size = NUM_RX_DESCS,
    .tx_size = NUM_TX_DESCS,
    .libbpf_flags = 0,
    .xdp_flags = _xfx->xdp_flags(),
    .bind_flags = _xfx->bind_flags(),
  };

  int ret = xsk_socket__create(
      &_xsk->xsk,
      _xfx->dev().c_str(),
      _queue_id,
      _umem->umem,
      &_xsk->rx,
      &_xsk->tx,
      &cfg
  );
  if (ret) {
    die("failed to create xsk socket", ret);
  }


  ret = bpf_get_link_xdp_id(_xfx->ifindex(), &_prog_id, _xfx->xdp_flags());
  if (ret) {
    die("failed to get bpf program id", ret);
  }

  if(_trace) {
    printf("ingress UMEM: 0x%08x - 0x%08x\n", 0, NUM_RX_DESCS*FRAME_SIZE);
    printf("egress UMEM:  0x%08x - 0x%08x\n", 
        NUM_RX_DESCS*FRAME_SIZE, 
        NUM_RX_DESCS*FRAME_SIZE + NUM_TX_DESCS*FRAME_SIZE
    );
  }

  // initialize the fill queue addresses - the places in the UMEM where the
  // kernel will place received packets
  for(size_t i = 0; i < NUM_RX_DESCS; i++) {

    auto *addr = xsk_ring_prod__fill_addr(&_xsk->umem->fq, i);
    *addr = i*FRAME_SIZE;

  }
  xsk_ring_prod__submit(&_xsk->umem->fq, NUM_RX_DESCS);

  // initialize the tx queue addresses - the places in the UMEM where the
  // application will place packets to transmit
  for(size_t i = 0; i < NUM_TX_DESCS; i++) {

    // TX frames are in the second partition of the UMEM after the RX frames,
    // hence the NUM_RX_DESCS offset
    auto *addr = xsk_ring_prod__fill_addr(&_xsk->tx, i);
    *addr = NUM_RX_DESCS*FRAME_SIZE + i*FRAME_SIZE;

  }

}

void XDPSock::rx(PBuf &pb) 
{

  static int cnt{0};
  pb.len = 0;
  u32 idx_rx{0};

  // peek into the rx ring
  if(_trace) {
    printf("[%s:%d] (%d) rx\n", _xfx->dev().c_str(), _queue_id, cnt++);
    printf("[%s:%d] rx peeking\n", _xfx->dev().c_str(), _queue_id);
  }
  uint rcvd = xsk_ring_cons__peek(&_xsk->rx, BATCH_SIZE, &idx_rx);

  // if there is nothing to be received, bail
  if (!rcvd) {
    return;
  }
  if(_trace) {
    printf("[%s:%d] rx rcvd=%u\n", _xfx->dev().c_str(), _queue_id, rcvd);
    printf("[%s:%d] rx reserving\n", _xfx->dev().c_str(), _queue_id);
  }

  // prepare to give the kernel new frames to replace the ones in the rx ring
  u32 idx_fq{0};
  int ret = xsk_ring_prod__reserve(&_xsk->umem->fq, rcvd, &idx_fq);
  while (ret != rcvd) {
    if (ret < 0) {
      die("queue reserve failed", ret);
    }
    printf("[%s:%d] rx reserving again\n", _xfx->dev().c_str(), _queue_id);
    ret = xsk_ring_prod__reserve(&_xsk->umem->fq, rcvd, &idx_fq);
  }

  for (size_t i = 0; i < rcvd; i++) {

    if(_trace) {
      printf("[%s:%d] rx accessing desc @%u\n", _xfx->dev().c_str(), _queue_id, idx_rx+i);
    }

    auto *desc = xsk_ring_cons__rx_desc(&_xsk->rx, idx_rx+i);

    if(_trace) {
      printf("[%s:%d] rx accessing data @%x\n", _xfx->dev().c_str(), _queue_id, desc->addr);
    }

    char *xsk_pkt = static_cast<char*>(
        xsk_umem__get_data(_xsk->umem->buffer, desc->addr)
    );

    /*
    char *click_pkt = static_cast<char*>( malloc(desc->len) );
    memcpy(click_pkt, xsk_pkt, desc->len);
    */

    WritablePacket* p =
        Packet::make(reinterpret_cast<unsigned char*>(xsk_pkt), desc->len,
                     free_pkt, xsk_pkt, FRAME_HEADROOM, FRAME_TAILROOM);
    p->timestamp_anno();
    p->set_anno_u32(7, _queue_id);
    pb.pkts[i] = p;

    if(_trace) {
      printf("[%s:%d] setting fill addr @%u\n", _xfx->dev().c_str(), _queue_id, idx_fq+i);
    }

    // TODO get next frame from UMEM manager and give to kernel to keep fill
    // queue as full as possible
    //*xsk_ring_prod__fill_addr(&_xsk->umem->fq, idx_fq+i) = desc->addr;
    xsk_ring_prod__fill_addr(&_xsk->umem->fq, _umem_mgr->next());
  }
  pb.len = rcvd;

  if(_trace) {
    printf("[%s:%d] rx submitting \n", _xfx->dev().c_str(), _queue_id);
  }
  xsk_ring_prod__submit(&_xsk->umem->fq, rcvd);

  if(_trace) {
    printf("[%s:%d] rx releasing\n", _xfx->dev().c_str(), _queue_id);
  }
  xsk_ring_cons__release(&_xsk->rx, rcvd);
  _xsk->rx_npkts += rcvd;

}

void XDPSock::tx(Packet *p)
{
  if(_trace) {
    printf("[%s]tx\n", _xfx->dev().c_str());
  }

  u32 idx{0};

  size_t avail = xsk_ring_prod__reserve(&_xsk->tx, 1, &idx);
  if (avail < 1) {
    printf("xdpsock: ring overflow\n");
    return;
  }


  xdp_desc *desc = xsk_ring_prod__tx_desc(&_xsk->tx, idx);
  desc->len = p->length();
  memcpy(
      xsk_umem__get_data(_xsk->umem->buffer, desc->addr),
      p->data(),
      p->length()
  );

  xsk_ring_prod__submit(&_xsk->tx, 1);
  _xsk->outstanding_tx++;

}

void XDPSock::kick()
{

  if (!_xsk->outstanding_tx) {
    return;
  }

  kick_tx();

  int n = _xsk->outstanding_tx > BATCH_SIZE ? BATCH_SIZE : _xsk->outstanding_tx;

  u32 idx{0};
  uint rcvd = xsk_ring_cons__peek(&_xsk->umem->cq, n, &idx);
  if (rcvd > 0) {
    xsk_ring_cons__release(&_xsk->umem->cq, rcvd);
    _xsk->outstanding_tx -= rcvd;
    _xsk->tx_npkts += rcvd;
  }


}

void XDPSock::kick_tx()
{

  int ret = sendto(xsk_socket__fd(_xsk->xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);
  if (ret >= 0 || errno == ENOBUFS || errno == EAGAIN || errno == EBUSY)
    return;

  die("failed to kick tx", ret);

}

pollfd XDPSock::poll_fd() const
{ 
  pollfd pfd = {
    .fd = xsk_socket__fd(_xsk->xsk), 
    .events = POLLIN
  }; 

  return pfd;
}
