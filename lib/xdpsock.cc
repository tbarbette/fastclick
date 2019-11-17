/*
 * xdpsock.{cc,hh} An expressi data path socket object (XDP)
 */

#include <click/xdpsock.hh>

using std::string;
using std::vector;

XDPSock::XDPSock(string ifname, u16 xdp_flags, u16 bind_flags)
  : _ifname{ifname},
    _xdp_flags{xdp_flags},
    _bind_flags{bind_flags}
{
  configure_umem();
  configure_socket();
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
    .xdp_flags = _xdp_flags,
    .bind_flags = _bind_flags,
  };

 
  //TODO something configurable?
  u32 queue_id{0};

  int ret = xsk_socket__create(
      &_xsk->xsk,
      _ifname.c_str(),
      queue_id,
      _umem->umem,
      &_xsk->rx,
      &_xsk->tx,
      &cfg
  );
  if (ret) {
    die("failed to create xsk socket", ret);
  }

  u32 ifindex = if_nametoindex(_ifname.c_str());
  if (!ifindex) {
    die("interface does not exist");
  }

  ret = bpf_get_link_xdp_id(ifindex, &_prog_id, _xdp_flags);
  if (ret) {
    die("failed to get bpf program id", ret);
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
    *addr = NUM_RX_DESCS + i*FRAME_SIZE;

  }

}

void XDPSock::configure_umem()
{

  int ret = posix_memalign(&_umem_buf, getpagesize(), NUM_FRAMES * FRAME_SIZE);
  if (ret) {
    die("failed to align umem buffer", ret);
  }

  xsk_umem_config cfg = {
		.fill_size = NUM_TX_DESCS,
		.comp_size = NUM_RX_DESCS,
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

}

vector<Packet*> XDPSock::rx() 
{
  u32 idx_rx{0};
  uint rcvd = xsk_ring_cons__peek(&_xsk->rx, BATCH_SIZE, &idx_rx);
  if (!rcvd) {
   return vector<Packet*>{};
  }

  u32 idx_fq{0};
  int ret = xsk_ring_prod__reserve(&_xsk->umem->fq, rcvd, &idx_fq);
  while (ret != rcvd) {
    if (ret < 0) {
      die("queue reserve failed", ret);
    }
    ret = xsk_ring_prod__reserve(&_xsk->umem->fq, rcvd, &idx_fq);
  }

  vector<Packet*> result(rcvd);

  for (size_t i = 0; i < rcvd; i++) {

    auto *desc = xsk_ring_cons__rx_desc(&_xsk->rx, idx_rx);
    char *xsk_pkt = static_cast<char*>(
        xsk_umem__get_data(_xsk->umem->buffer, desc->addr)
    );

    char *click_pkt = static_cast<char*>( malloc(desc->len) );
    memcpy(click_pkt, xsk_pkt, desc->len);

    WritablePacket *p = Packet::make(
        reinterpret_cast<unsigned char*>(click_pkt),
        desc->len,
        free_pkt,
        click_pkt,
        FRAME_HEADROOM,
        FRAME_TAILROOM
    );
    result.push_back(p);

  }

  xsk_ring_prod__submit(&_xsk->umem->fq, rcvd);
  xsk_ring_cons__release(&_xsk->rx, rcvd);
  _xsk->rx_npkts += rcvd;

  return result;
  
}

void XDPSock::tx(Packet *p)
{

  u32 idx{0};

  size_t avail = xsk_ring_prod__reserve(&_xsk->tx, 1, &idx);
  if (avail < 1) {
    printf("xdpsock: ring overflow\n");
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

  u32 idx{0};
  uint rcvd = xsk_ring_cons__peek(&_xsk->umem->cq, BATCH_SIZE, &idx);
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
