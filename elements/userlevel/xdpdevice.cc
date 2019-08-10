/*
 * xdpdevice.{cc,hh} XDP to/from device implementation
 *
 * Ryan Goodfellow
 *
 * Copyright (c) 2019 mergetb.org
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include "xdpdevice.hh"
#include <click/args.hh>
#include <click/error.hh>

extern "C" {
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <poll.h>
#include <time.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <net/if.h>
#include <sys/mman.h>
#include <linux/bpf.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <linux/if_xdp.h>
#include <linux/if_link.h>
}

CLICK_DECLS

u32 XDPDevice::xq_nb_avail(struct xdp_uqueue *q, u32 nb)
{
  u32 entries = q->cached_prod - q->cached_cons;
  //printf("entries: %u\n", entries);

  if (entries == 0) {
    q->cached_prod = *q->producer;
    entries = q->cached_prod - q->cached_cons;
  }

  return (entries > nb) ? nb : entries;
}

int XDPDevice::xq_deq(struct xdp_uqueue *uq, struct xdp_desc *descs, int ndescs)
{
  struct xdp_desc *r = uq->ring;
  int entries = xq_nb_avail(uq, ndescs);

  u_smp_rmb();

  for (int i = 0; i < entries; i++) {
    int idx = uq->cached_cons++ & uq->mask;
    descs[i] = r[idx];
  }

  if (entries > 0) {
    u_smp_wmb();
    *uq->consumer = uq->cached_cons;
  }

  return entries;
}

void XDPDevice::hex_dump(void *pkt, size_t length, u64 addr)
{
	const unsigned char *address = (unsigned char *)pkt;
	const unsigned char *line = address;
	size_t line_size = 32;
	unsigned char c;
	char buf[32];
	int i = 0;

	sprintf(buf, "addr=%lu", addr);
	printf("length = %zu\n", length);
	printf("%s | ", buf);
	while (length-- > 0) {
		printf("%02X ", *address++);
		if (!(++i % line_size) || (length == 0 && i % line_size)) {
			if (length == 0) {
				while (i++ % line_size)
					printf("__ ");
			}
			printf(" | ");	/* right close */
			while (line < address) {
				c = *line++;
				printf("%c", (c < 33 || c == 255) ? 0x2E : c);
			}
			printf("\n");
			if (length > 0)
				printf("%s | ", buf);
		}
	}
	printf("\n");
}

void *XDPDevice::xq_get_data(struct xdpsock *xsk, u64 addr)
{
  return &xsk->umem->frames[addr];
}


u32 XDPDevice::umem_nb_free(struct xdp_umem_uqueue *q, u32 nb)
{
  u32 free_entries = q->cached_cons - q->cached_prod;

  if (free_entries >= nb)
    return free_entries;

  q->cached_cons = *q->consumer + q->size;

  return q->cached_cons - q->cached_prod;
}

int XDPDevice::umem_fill_to_kernel_ex(struct xdp_umem_uqueue *fq, struct xdp_desc *d, size_t nb)
{
  if (umem_nb_free(fq, nb) < nb)
    return -ENOSPC;

  for (u32 i = 0; i < nb; i++) {
    u32 idx = fq->cached_prod++ & fq->mask;
    fq->ring[idx] = d[i].addr;
  }

  u_smp_wmb();
  *fq->producer = fq->cached_prod;

  return 0;
}

int XDPDevice::umem_fill_to_kernel(struct xdp_umem_uqueue *fq, u64 *d,
    size_t nb) 
{

  if (umem_nb_free(fq, nb) < nb)
    return -ENOSPC;

  for (size_t i = 0; i < nb; i++) {
    u32 idx = fq->cached_prod++ & fq->mask;
    fq->ring[idx] = d[i];
  }

  u_smp_wmb();
  *fq->producer = fq->cached_prod;

  return 0;
}

struct xdp_umem *XDPDevice::umem_config(int sfd) 
{

  struct xdp_umem *umem;
  void *bufs;

  umem = (xdp_umem*)calloc(1, sizeof(struct xdp_umem));
  if (umem == NULL) {
    fprintf(stderr, "%s\n", "unable to allocate umem struct");
    exit(EXIT_FAILURE);
  }

  int err = posix_memalign(&bufs, getpagesize(), NUM_FRAMES * FRAME_SIZE);
  if (err) {
    fprintf(stderr, "failed to align buffer memory: %s\n", strerror(err));
    exit(EXIT_FAILURE);
  }

  struct xdp_umem_reg mr = {
    .addr = (u64)bufs,
    .len = NUM_FRAMES * FRAME_SIZE,
    .chunk_size = FRAME_SIZE,
    .headroom = FRAME_HEADROOM,
  };

  err = setsockopt(sfd, SOL_XDP, XDP_UMEM_REG, &mr, sizeof(mr));
  if (err) {
    fprintf(stderr, "failed to register umem: %s\n", strerror(err));
    exit(EXIT_FAILURE);
  }

  int fq_size = FQ_NUM_DESCS,
      cq_size = CQ_NUM_DESCS;

  err = setsockopt(sfd, SOL_XDP, XDP_UMEM_FILL_RING, &fq_size, sizeof(int));
  if (err) {
    fprintf(stderr, "failed to set fill ring: %s\n", strerror(err));
    exit(EXIT_FAILURE);
  }

  err = setsockopt(sfd, SOL_XDP, XDP_UMEM_COMPLETION_RING, &cq_size, sizeof(int));
  if (err) {
    fprintf(stderr, "failed to set completion ring: %s\n", strerror(err));
    exit(EXIT_FAILURE);
  }

  struct xdp_mmap_offsets off;
  printf("off.fr.producer %llx\n", off.fr.producer);
  printf("off.fr.consumer %llx\n", off.fr.consumer);
  socklen_t optlen = sizeof(struct xdp_mmap_offsets);
  err = getsockopt(sfd, SOL_XDP, XDP_MMAP_OFFSETS, &off, &optlen);
  printf("off.fr.producer %llx\n", off.fr.producer);
  printf("off.fr.consumer %llx\n", off.fr.consumer);
  if (err) {
    fprintf(stderr, "failed to get xdp mmap offsets: %s\n", strerror(err));
    exit(EXIT_FAILURE);
  }

  // fill ring
  umem->fq.map = mmap(
      0,
      off.fr.desc + FQ_NUM_DESCS*sizeof(u64),
      PROT_READ | PROT_WRITE,
      MAP_SHARED | MAP_POPULATE,
      sfd,
      XDP_UMEM_PGOFF_FILL_RING
      );
  if (umem->fq.map == MAP_FAILED) {
    fprintf(stderr, "%s\n", "failed to map fill ring");
    exit(EXIT_FAILURE);
  }

  umem->fq.mask = FQ_NUM_DESCS - 1;
  umem->fq.size = FQ_NUM_DESCS;
  umem->fq.producer = (u32*)(umem->fq.map + off.fr.producer);
  umem->fq.consumer = (u32*)(umem->fq.map + off.fr.consumer);
  umem->fq.ring = (u64*)(umem->fq.map + off.fr.desc);
  umem->fq.cached_cons = FQ_NUM_DESCS;

  // completion ring
  umem->cq.map = mmap(
      0,
      off.cr.desc + CQ_NUM_DESCS*sizeof(u64),
      PROT_READ | PROT_WRITE,
      MAP_SHARED | MAP_POPULATE,
      sfd,
      XDP_UMEM_PGOFF_COMPLETION_RING
  );
  if (umem->cq.map == MAP_FAILED) {
    fprintf(stderr, "%s\n", "failed to map completion ring");
    exit(EXIT_FAILURE);
  }

  umem->cq.mask = CQ_NUM_DESCS - 1;
  umem->cq.size = CQ_NUM_DESCS;
  umem->cq.producer = (u32*)(umem->cq.map + off.cr.producer);
  umem->cq.consumer = (u32*)(umem->cq.map + off.cr.consumer);
  umem->cq.ring = (u64*)(umem->cq.map + off.cr.desc);

  umem->frames = (char*)bufs;
  umem->fd = sfd;

  return umem;

}

int XDPDevice::configure(Vector<String> &conf, ErrorHandler *errh) {

  if(Args(conf, this, errh)
      .read_mp("DEV", _dev)
      .read_or_set("MODE", _mode, "skb")
      .consume() < 0 ) {

    return CONFIGURE_FAIL;
  
  }

  _ifx_index = if_nametoindex(_dev.c_str());
  if (!_ifx_index) {
    errh->error("interface \"%s\" does not exist", _dev.c_str());
    return CONFIGURE_FAIL;
  }

  if (_mode == "xdp") {
    _flags |= XDP_FLAGS_DRV_MODE;
  }
  else if (_mode == "skb") {
    _flags |= XDP_FLAGS_SKB_MODE;
  }
  else if (_mode == "copy") {
    _flags |= XDP_FLAGS_SKB_MODE;
    _bind_flags |= XDP_COPY;
  }
  else {
    errh->error("invalid mode \"%s\" must be (xdp|skb|copy)");
    return CONFIGURE_FAIL;
  }

  return CONFIGURE_SUCCESS;

}

void XDPDevice::set_rlimit(ErrorHandler *errh) {

  struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
  int err = setrlimit(RLIMIT_MEMLOCK, &r);
  if (err) {
    errh->fatal("failed to set rlimit: %s", strerror(err));
  }

}

void XDPDevice::init_bpf(ErrorHandler *errh) {

  // load program

  struct bpf_prog_load_attr pla = {
    .file = "/usr/lib/click/xdpbpf.o",
    .prog_type = BPF_PROG_TYPE_XDP,
  };

  printf("cats\n");

  struct bpf_object *bpf_obj;
  int bpf_fd;

  int err = bpf_prog_load_xattr(&pla, &bpf_obj, &bpf_fd);
  if (err) {
    errh->fatal("failed to load bpf program %s: %s", pla.file, strerror(err));
  }
  if (bpf_fd < 0) {
    errh->fatal("failed to load bpf program (fd): %s", strerror(bpf_fd));
  }

  printf("bananas\n");

  // load socket map

  _xsk_map = bpf_object__find_map_by_name(bpf_obj, "xsk_map");
  _xsk_map_fd = bpf_map__fd(_xsk_map);
  if (_xsk_map_fd < 0) {
    errh->fatal("failed to load xsk_map: %s", strerror(_xsk_map_fd));
  }

  // load queue id config map
  _qidconf_map = bpf_object__find_map_by_name(bpf_obj, "qidconf_map");
  _qidconf_map_fd = bpf_map__fd(_qidconf_map);
  if (_qidconf_map_fd < 0) {
    errh->fatal("failed to load qidconf_map: %s", strerror(_qidconf_map_fd));
  }

  int key = 0, q = 0;
  err = bpf_map_update_elem(_qidconf_map_fd, &key, &q, 0);
  if (err) {
    errh->fatal("failed to configure qidconf map: %s", strerror(err));
  }

  // apply the bpf program to the specified link

  err = bpf_set_link_xdp_fd(_ifx_index, bpf_fd, _flags);
  if(err < 0) {
    errh->fatal("xdp link set failed: %s", strerror(err));
  }

}

void XDPDevice::init_xsk(ErrorHandler *errh) {

  _sfd = socket(AF_XDP, SOCK_RAW, 0);
  if(_sfd < 0) {
    errh->fatal("socket failed: %s", strerror(_sfd));
  }

  _xsk = (xdpsock*)calloc(1, sizeof(struct xdpsock));
  if(_xsk == NULL) {
    errh->fatal("%s", "failed to allocated xdp socket");
    exit(EXIT_FAILURE);
  }
  _xsk->sfd = _sfd;
  _xsk->outstanding_tx = 0;
  _xsk->rx_npkts = 0;
  _xsk->tx_npkts = 0;
  _xsk->prev_rx_npkts = 0;
  _xsk->prev_tx_npkts = 0;

  _xsk->umem = umem_config(_sfd);

  int ndescs = NUM_DESCS;
  int err = setsockopt(_sfd, SOL_XDP, XDP_RX_RING, &ndescs, sizeof(int));
  if (err) {
    errh->fatal("failed to set rx ring descriptor: %s", strerror(err));
  }
  err = setsockopt(_sfd, SOL_XDP, XDP_TX_RING, &ndescs, sizeof(int));
  if (err) {
    errh->fatal("failed to set tx ring descriptor: %s", strerror(err));
  }

  struct xdp_mmap_offsets off;
  socklen_t optlen = sizeof(struct xdp_mmap_offsets);
  err = getsockopt(_sfd, SOL_XDP, XDP_MMAP_OFFSETS, &off, &optlen);
  if (err) {
    errh->fatal("failed to get mmap offsets: %s", strerror(err));
  }

  // rx 
  _xsk->rx.map = mmap(
      NULL,
      off.rx.desc + NUM_DESCS * sizeof(struct xdp_desc),
      PROT_READ | PROT_WRITE,
      MAP_SHARED | MAP_POPULATE,
      _sfd,
      XDP_PGOFF_RX_RING
  );
  if (_xsk->rx.map == MAP_FAILED) {
    errh->fatal("failed to mmap rx ring");
  }

  for (u64 i=0; i < NUM_DESCS*FRAME_SIZE; i += FRAME_SIZE) {
    err = umem_fill_to_kernel(&_xsk->umem->fq, &i, 1);
    if (err) {
      errh->fatal("failed to fill rx frame to kernel: %s", strerror(err));
    }
  }

  // tx 
  _xsk->tx.map = mmap(
      NULL,
      off.tx.desc + NUM_DESCS * sizeof(struct xdp_desc),
      PROT_READ | PROT_WRITE,
      MAP_SHARED | MAP_POPULATE,
      _sfd,
      XDP_PGOFF_TX_RING
  );
  if (_xsk->tx.map == MAP_FAILED) {
    errh->fatal("failed to mmap tx ring");
  }

  _xsk->rx.mask = NUM_DESCS - 1;
  _xsk->rx.size = NUM_DESCS;
  _xsk->rx.producer = (u32*)(_xsk->rx.map + off.rx.producer);
  _xsk->rx.consumer = (u32*)(_xsk->rx.map + off.rx.consumer);
  _xsk->rx.ring = (xdp_desc*)(_xsk->rx.map + off.rx.desc);

  _xsk->tx.mask = NUM_DESCS - 1;
  _xsk->tx.size = NUM_DESCS;
  _xsk->tx.producer = (u32*)(_xsk->tx.map + off.tx.producer);
  _xsk->tx.consumer = (u32*)(_xsk->tx.map + off.tx.consumer);
  _xsk->tx.ring = (xdp_desc*)(_xsk->tx.map + off.tx.desc);
  _xsk->tx.cached_cons = NUM_DESCS;

  struct sockaddr_xdp sxdp = {
    .sxdp_family = AF_XDP,
    .sxdp_flags = _bind_flags,
    .sxdp_ifindex = _ifx_index,
    .sxdp_queue_id = 0,
  };

  err = bind(_sfd, (struct sockaddr *)&sxdp, sizeof(sxdp));
  if (err) {
    errh->fatal("failed to bind xdp socket: %s", strerror(err));
  }

  // insert xdp sockets into map

  int key = 0;
  err = bpf_map_update_elem(_xsk_map_fd, &key, &_xsk->sfd, 0);
  if (err) {
    errh->fatal("failed to add socket to map: %s", strerror(err));
  }

}

u32 XDPDevice::xq_nb_free(struct xdp_uqueue *q, u32 ndescs)
{
  u32 free_entries = q->cached_cons - q->cached_prod;

  if(free_entries >= ndescs)
    return free_entries;

  q->cached_cons = *q->consumer + q->size;
  return q->cached_cons - q->cached_prod;
}

void XDPDevice::kick_tx(int fd)
{
  int ret = sendto(fd, NULL, 0, MSG_DONTWAIT, NULL, 0);
  if (ret >= 0 || errno == ENOBUFS || errno == EAGAIN || errno == EBUSY)
    return;

  fprintf(stderr, "failed to kick tx: %s\n", strerror(ret));
  exit(EXIT_FAILURE);
}

size_t XDPDevice::umem_complete_from_kernel( struct xdp_umem_uqueue *cq, u64 *d, size_t nb)
{
  u32 idx, i, entries = umem_nb_avail(cq, nb);

  u_smp_rmb();

  for (i = 0; i < entries; i++) {
    idx = cq->cached_cons++ & cq->mask;
    d[i] = cq->ring[idx];
  }

  if (entries > 0) {
    printf("kernel completed %d entries\n", entries);
    u_smp_wmb();
    *cq->consumer = cq->cached_cons;
  }

  return entries;
}

u32 XDPDevice::umem_nb_avail(struct xdp_umem_uqueue *q, u32 nb)
{
  u32 entries = q->cached_prod - q->cached_cons;

  if (entries == 0) {
    q->cached_prod = *q->producer;
    entries = q->cached_prod - q->cached_cons;
  }

  return (entries > nb) ? nb : entries;
}

// ~~~~ from ~~~~

int XDPDevice::initialize(ErrorHandler *errh) {

  set_rlimit(errh);
  init_bpf(errh);
  init_xsk(errh);

  _t = new Task(this);
  _t->initialize(this, true);

  return INITIALIZE_SUCCESS;

}

static void free_pkt(unsigned char *, size_t, void *pktmbuf)
{
   //TODO, possibly no need for anything ans umem_fill_to_kernel_ex churns the
   //ring?
}

bool XDPDevice::run_task(Task *t) 
{

  push();
  pull();
  t->fast_reschedule();
  return true;
}

void XDPDevice::push()
{
  struct xdp_desc descs[BATCH_SIZE];
  unsigned int rcvd = xq_deq(&_xsk->rx, descs, BATCH_SIZE);
  if (rcvd == 0) {
    return;
  }
  printf("recvd: %u\n", rcvd);

  for (unsigned int i = 0; i < rcvd; i++) {
    char *pkt = (char*)xq_get_data(_xsk, descs[i].addr);
    hex_dump(pkt, descs[i].len, descs[i].addr);

    //TODO totally untested
    WritablePacket *p = Packet::make(
        (unsigned char*)pkt,
        descs[i].len,
        free_pkt,
        pkt,
        FRAME_HEADROOM,
        FRAME_TAILROOM
    );
    output(0).push(p);
  }

  _xsk->rx_npkts += rcvd;

  umem_fill_to_kernel_ex(&_xsk->umem->fq, descs, rcvd);


}

// ~~~~ to ~~~~



void XDPDevice::pull()
{
  struct xdp_uqueue *uq = &_xsk->tx;
  struct xdp_desc *r = uq->ring;

  kick_tx(_xsk->sfd);

  for (Packet *p = input(0).pull(); p != nullptr; p = input(0).pull()) {

    printf("%s sending packet (%d)\n", name().c_str(), p->length());
    hex_dump((void*)p->data(), p->length(), 1701);

    if (xq_nb_free(uq, 1) < 1) {
      click_chatter("toxdp: ring overflow");
      return;
    }

    u32 idx = uq->cached_prod++ & uq->mask;
    u64 addr = idx << FRAME_SHIFT;
    r[idx].addr = addr;
    r[idx].len = p->length();
    memcpy(
        &_xsk->umem->frames[addr],
        p->data(),
        p->length()
    );

  }

  u_smp_wmb();
  *uq->producer = uq->cached_prod;
  _xsk->outstanding_tx++;

  u64 descs[BATCH_SIZE];
  size_t ndescs = 
    (_xsk->outstanding_tx > BATCH_SIZE) ? BATCH_SIZE : _xsk->outstanding_tx;
  unsigned int rcvd = umem_complete_from_kernel(&_xsk->umem->cq, descs, ndescs);
  if (rcvd > 0) {
    umem_fill_to_kernel(&_xsk->umem->fq, descs, rcvd);
    _xsk->outstanding_tx -= rcvd;
    _xsk->tx_npkts += rcvd;
  }


  kick_tx(_xsk->sfd);

}

CLICK_ENDDECLS

EXPORT_ELEMENT(XDPDevice)
