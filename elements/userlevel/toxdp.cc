/*
 * fromxdp.{cc,hh} writes network packets to a linux netdev via XDP
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

#include "toxdp.hh"
#include <click/args.hh>
#include <click/error.hh>
#include <click/xdp_common.hh>

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

void ToXDP::push(int port, Packet *p)
{

  struct xdp_uqueue *uq = &_xsk->tx;
  struct xdp_desc *r = uq->ring;

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

EXPORT_ELEMENT(ToXDP)
