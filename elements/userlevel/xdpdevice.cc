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

#include <array>
#include <memory>
#include "xdpdevice.hh"
#include "json.hpp"
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

using std::string;
using std::array;
using std::unique_ptr;
using nlohmann::json;

#ifndef __NR_bpf
# if defined(__i386__)
#  define __NR_bpf 357
# elif defined(__x86_64__)
#  define __NR_bpf 321
# elif defined(__aarch64__)
#  define __NR_bpf 280
# elif defined(__sparc__)
#  define __NR_bpf 349
# elif defined(__s390__)
#  define __NR_bpf 351
# else
#  error __NR_bpf not defined. bpf does not support your arch.
# endif
#endif

static string exec(const char* cmd) {
    array<char, 128> buffer;
    string result;
    unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

static int bpf(int cmd, union bpf_attr *attr, unsigned int size)
{
	return syscall(__NR_bpf, cmd, attr, size);
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


// calculate the number of free entries in a queue
u32 XDPDevice::umem_nb_free(struct xdp_umem_uqueue *q, u32 nb)
{
  u32 free_entries = q->cached_cons - q->cached_prod;

  if (free_entries >= nb)
    return free_entries;

  q->cached_cons = *q->consumer + q->size;

  return q->cached_cons - q->cached_prod;
}

// send descriptors to the kernel
int XDPDevice::umem_fill_to_kernel_ex(struct xdp_umem_uqueue *q, struct xdp_desc *d, size_t nb)
{
  // see if there is enough space in the queue
  if (umem_nb_free(q, nb) < nb)
    return -ENOSPC;

  // add the descriptors to the ring, updating the cached producer index
  for (size_t i = 0; i < nb; i++) {
    u32 idx = q->cached_prod++ & q->mask;
    q->ring[idx] = d[i].addr;
  }

  // write through the cached producer to the producer variable after a memory
  // barrier
  u_smp_wmb();
  *q->producer = q->cached_prod;

  return 0;
}

// send descriptors to the kernel by address
int XDPDevice::umem_fill_to_kernel(struct xdp_umem_uqueue *q, u64 *d,
    size_t nb) 
{

  // see if there is enough space in the queue
  if (umem_nb_free(q, nb) < nb)
    return -ENOSPC;

  // add the descriptors to the ring, updating the cached producer index
  for (size_t i = 0; i < nb; i++) {
    u32 idx = q->cached_prod++ & q->mask;
    q->ring[idx] = d[i];
  }

  // write through the cached producer to the producer variable after a memory
  // barrier
  u_smp_wmb();
  *q->producer = q->cached_prod;

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
  if (_trace) {
    printf("off.fr.producer %llx\n", off.fr.producer);
    printf("off.fr.consumer %llx\n", off.fr.consumer);
  }
  socklen_t optlen = sizeof(struct xdp_mmap_offsets);
  err = getsockopt(sfd, SOL_XDP, XDP_MMAP_OFFSETS, &off, &optlen);
  if (_trace) {
    printf("off.fr.producer %llx\n", off.fr.producer);
    printf("off.fr.consumer %llx\n", off.fr.consumer);
  }
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

  String entry;

  if(Args(conf, this, errh)
      .read_mp("DEV", _dev)
      .read_or_set("MODE", _mode, "skb")
      .read_or_set("PROG", _prog, "xdpallrx")
      .read_or_set("LOAD", _load, true)
      .read_or_set("VNI", _vni, 0)
      .read_or_set("TRACE", _trace, false)
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

  _prog_file = String("/usr/lib/click/")+ _prog + String(".o");

  return CONFIGURE_SUCCESS;

}

void XDPDevice::set_rlimit(ErrorHandler *errh) {

  struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
  int err = setrlimit(RLIMIT_MEMLOCK, &r);
  if (err) {
    errh->fatal("failed to set rlimit: %s", strerror(err));
  }

}

void XDPDevice::load_bpf_program(ErrorHandler *errh) 
{
  struct bpf_prog_load_attr pla = {
    .file = _prog_file.c_str(),
    .prog_type = BPF_PROG_TYPE_XDP,
  };

  int err = bpf_prog_load_xattr(&pla, &_bpf_obj, &_bpf_fd);
  if (err) {
    errh->fatal("failed to load bpf program %s: %s", pla.file, strerror(err));
  }
  if (_bpf_fd < 0) {
    errh->fatal("failed to load bpf program (fd): %s", strerror(_bpf_fd));
  }

  // apply the bpf program to the specified link
  err = bpf_set_link_xdp_fd(_ifx_index, _bpf_fd, _flags);
  if(err < 0) {
    errh->fatal("xdp link set failed: %s", strerror(err));
  }
}

void XDPDevice::open_bpf_program(ErrorHandler *errh)
{
  struct bpf_object_open_attr attr = {
    .file = _prog_file.c_str(),
    .prog_type = BPF_PROG_TYPE_XDP,
  };

  _bpf_obj = bpf_object__open_xattr(&attr);
  if (IS_ERR_OR_NULL(_bpf_obj))
    errh->fatal("failed to open bpf object");

  string cmd = "ip -j link show dev " + string(_dev.c_str());
  string out = exec(cmd.c_str());

  json j = json::parse(out);
  int prog_id = j[0]["xdp"]["prog"]["id"];

  union bpf_attr battr = {};
  battr.prog_id = prog_id;
  _bpf_fd = bpf(BPF_PROG_GET_FD_BY_ID, &battr, sizeof(battr));
  if (_bpf_fd < 0)
    errh->fatal("failed to get bpf program fd using id: %s", strerror(_bpf_fd));

  /*
  struct bpf_program *prog = bpf_program__next(NULL, _bpf_obj);
  if (IS_ERR_OR_NULL(_bpf_obj))
    errh->fatal("failed to open bpf program");

  _bpf_fd = bpf_program__fd(prog);
  if (_bpf_fd < 0) {
    errh->fatal("failed to load bpf program (fd): %s", strerror(_bpf_fd));
  }
  */

}

void XDPDevice::load_bpf_maps(ErrorHandler *errh)
{
  // load socket map
  _xsk_map = bpf_object__find_map_by_name(_bpf_obj, "xsk_map");
  if (IS_ERR_OR_NULL(_xsk_map))
    errh->fatal("could not find xsk_map");

  _xsk_map_fd = bpf_map__fd(_xsk_map);
  if (_xsk_map_fd < 0) {
    errh->fatal("failed to load xsk_map: %s", strerror(_xsk_map_fd));
  }

  // load vni map
  if (_vni) {
    _vni_map = bpf_object__find_map_by_name(_bpf_obj, "vni_map");
    _vni_map_fd = bpf_map__fd(_vni_map);
    if (_vni_map_fd < 0) {
      errh->fatal("failed to load vni_map: %s", strerror(_vni_map_fd));
    }
  }

  // load queue id config map
  _qidconf_map = bpf_object__find_map_by_name(_bpf_obj, "qidconf_map");
  _qidconf_map_fd = bpf_map__fd(_qidconf_map);
  if (_qidconf_map_fd < 0) {
    errh->fatal("failed to load qidconf_map: %s", strerror(_qidconf_map_fd));
  }
}

void XDPDevice::init_bpf_qidconf(ErrorHandler *errh)
{
  int key = 0, q = 0;
  int err = bpf_map_update_elem(_qidconf_map_fd, &key, &q, 0);
  if (err) {
    errh->fatal("failed to configure qidconf map: %s", strerror(err));
  }
}

void XDPDevice::init_bpf(ErrorHandler *errh) 
{
  if (_load) 
    load_bpf_program(errh);
  else 
    open_bpf_program(errh);

  load_bpf_maps(errh);
  init_bpf_qidconf(errh);
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

  int ndescs = FQ_NUM_DESCS;
  int err = setsockopt(_sfd, SOL_XDP, XDP_RX_RING, &ndescs, sizeof(int));
  if (err) {
    errh->fatal("failed to set rx ring descriptor: %s", strerror(err));
  }

  ndescs = CQ_NUM_DESCS;
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
      off.rx.desc + FQ_NUM_DESCS * sizeof(struct xdp_desc),
      PROT_READ | PROT_WRITE,
      MAP_SHARED | MAP_POPULATE,
      _sfd,
      XDP_PGOFF_RX_RING
  );
  if (_xsk->rx.map == MAP_FAILED) {
    errh->fatal("failed to mmap rx ring");
  }

  for (u64 i=0; i < FQ_NUM_DESCS*FRAME_SIZE; i += FRAME_SIZE) {
    err = umem_fill_to_kernel(&_xsk->umem->fq, &i, 1);
    if (err) {
      errh->fatal("failed to fill rx frame to kernel: %s", strerror(err));
    }
  }

  // tx 
  _xsk->tx.map = mmap(
      NULL,
      off.tx.desc + CQ_NUM_DESCS * sizeof(struct xdp_desc),
      PROT_READ | PROT_WRITE,
      MAP_SHARED | MAP_POPULATE,
      _sfd,
      XDP_PGOFF_TX_RING
  );
  if (_xsk->tx.map == MAP_FAILED) {
    errh->fatal("failed to mmap tx ring");
  }

  _xsk->rx.mask = FQ_NUM_DESCS - 1;
  _xsk->rx.size = FQ_NUM_DESCS;
  _xsk->rx.producer = (u32*)(_xsk->rx.map + off.rx.producer);
  _xsk->rx.consumer = (u32*)(_xsk->rx.map + off.rx.consumer);
  _xsk->rx.ring = (xdp_desc*)(_xsk->rx.map + off.rx.desc);

  _xsk->tx.mask = CQ_NUM_DESCS - 1;
  _xsk->tx.size = CQ_NUM_DESCS;
  _xsk->tx.producer = (u32*)(_xsk->tx.map + off.tx.producer);
  _xsk->tx.consumer = (u32*)(_xsk->tx.map + off.tx.consumer);
  _xsk->tx.ring = (xdp_desc*)(_xsk->tx.map + off.tx.desc);
  _xsk->tx.cached_cons = CQ_NUM_DESCS;

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
  if (_vni) {

    printf("%s adding vni map [%d]%d -> %d\n", 
        name().c_str(), _vni_map_fd, _vni, _xsk->sfd);

    err = bpf_map_update_elem(_vni_map_fd, &_vni, &_xsk->sfd, 0);
    if (err) {
      errh->fatal("failed to add socket to map: %s", strerror(err));
    }
  }

  err = bpf_map_update_elem(_xsk_map_fd, &_vni, &_xsk->sfd, 0);
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
    if (_trace) {
      //printf("%s) kernel completed %d entries\n", name().c_str(), entries);
    }
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

static void free_pkt(unsigned char *pkt, size_t, void *pktmbuf)
{
   // right now we copy packets into click. an optimiazation that could be nice
   // is that instead of allocating a chunk of memory for a packet we 'allocate'
   // a frame from the destination xdp device. this is not really allocation in
   // the sense that all the frames for the device are allocated up front, it's
   // more of a taking of ownership. This would eliminate the need for any
   // dynamic memory allocation. However we would still need to copy the packet
   // from the source UMEM to the target UMEM.
   free(pkt);
}

bool XDPDevice::run_task(Task *t) 
{

  push();
  pull();
  t->fast_reschedule();
  return true;

}

// process packets received from the kernel
void XDPDevice::push()
{
  static struct xdp_desc descs[BATCH_SIZE];
  unsigned int rcvd = xq_deq(&_xsk->rx, descs, BATCH_SIZE);
  if (rcvd == 0) {
    return;
  }
  if (_trace) {
    printf("%s) recvd: %u\n", name().c_str(), rcvd);
  }

  for (unsigned int i = 0; i < rcvd; i++) {
    char *pkt = (char*)xq_get_data(_xsk, descs[i].addr);

    if (_trace) {
      //hex_dump(pkt, descs[i].len, descs[i].addr);
    }

    char *_pkt = (char*)malloc(descs[i].len);
    memcpy(_pkt, pkt, descs[i].len);

    WritablePacket *p = Packet::make(
        (unsigned char*)_pkt,
        descs[i].len,
        free_pkt,
        _pkt,
        FRAME_HEADROOM,
        FRAME_TAILROOM
    );
    output(0).push(p);
  }

  _xsk->rx_npkts += rcvd;

  if (rcvd > 0) {
    umem_fill_to_kernel_ex(&_xsk->umem->fq, descs, rcvd);
  }


}

// ~~~~ to ~~~~


// process packets received from click
void XDPDevice::pull()
{
  struct xdp_uqueue *uq = &_xsk->tx;
  struct xdp_desc *r = uq->ring;

  //do_tx();

  //kick_tx(_xsk->sfd);

  size_t i{0};
  for(i=0; i<BATCH_SIZE; i++) {

    Packet *p = input(0).pull();
    if (p == nullptr) {
      break;
    }

    if (_trace) {
      //printf("%s sending packet (%d)\n", name().c_str(), p->length());
      //hex_dump((void*)p->data(), p->length(), 1701);
    }

    //printf("%s) tx-free %d\n", name().c_str(), xq_nb_free(uq, 1));
    if (xq_nb_free(uq, 1) < 1) {
      printf("toxdp: ring overflow\n");
      printf("POOP %i %d\n", i, _xsk->outstanding_tx);
      exit(1);
      break;
    }

    u32 idx = uq->cached_prod++ & uq->mask;
    u64 addr = (idx + FQ_NUM_DESCS) << FRAME_SHIFT;
    r[idx].addr = addr;
    r[idx].len = p->length();
    memcpy(
        &_xsk->umem->frames[addr],
        p->data(),
        p->length()
    );
    p->kill();


  }

  u_smp_wmb();
  *uq->producer = uq->cached_prod;
  _xsk->outstanding_tx += i;

  /*
  if (_xsk->outstanding_tx > 47) {
    printf("TX SIZE %d\n", _xsk->outstanding_tx);
  }
  */

  /*
  if (i > 0) {
    printf("%s) sending %d,%d\n", name().c_str(), i, _xsk->outstanding_tx);
  }
  */

  do_tx();


}

void XDPDevice::do_tx()
{

  if (_xsk->outstanding_tx > 0) {
    kick_tx(_xsk->sfd);
  }

  u64 descs[BATCH_SIZE];
  size_t ndescs = 
    (_xsk->outstanding_tx > BATCH_SIZE) ? BATCH_SIZE : _xsk->outstanding_tx;

  unsigned int rcvd{0};
  if (ndescs > 0) {
    rcvd = umem_complete_from_kernel(&_xsk->umem->cq, descs, ndescs);
  }

  if (rcvd > 0) {
    _xsk->outstanding_tx -= rcvd;
    _xsk->tx_npkts += rcvd;
  }

}

CLICK_ENDDECLS

//EXPORT_ELEMENT(XDPDevice)

