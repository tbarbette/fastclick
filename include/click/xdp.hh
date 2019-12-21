#pragma once

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

#include <vector>
#include <string>
#include <memory>

#include <click/config.h>
#include <click/packet.hh>

#ifndef AF_XDP
#define AF_XDP 44
#endif

#ifndef SOL_XDP
#define SOL_XDP 283
#endif

#define barrier() __asm__ __volatile__("": : :"memory")
#define u_smp_wmb() barrier()
#define u_smp_rmb() barrier()

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

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define FRAME_SIZE    XSK_UMEM__DEFAULT_FRAME_SIZE
#define NUM_RX_DESCS  XSK_RING_CONS__DEFAULT_NUM_DESCS
#define NUM_TX_DESCS  XSK_RING_PROD__DEFAULT_NUM_DESCS
#define NUM_DESCS     (NUM_RX_DESCS + NUM_TX_DESCS)
#define NUM_FRAMES    NUM_DESCS
#define BATCH_SIZE    64
#define FRAME_HEADROOM XSK_UMEM__DEFAULT_FRAME_HEADROOM
#define FRAME_TAILROOM FRAME_HEADROOM

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

using std::string;
using std::vector;

class XDPSock;
class XDPInterface;
class XDPManager;

using XDPSockSP = std::shared_ptr<XDPSock>;
using XDPInterfaceSP = std::shared_ptr<XDPInterface>;

struct PBuf {
  std::array<Packet*, BATCH_SIZE> pkts{};
  size_t len{0};
};

static inline void die(const char *msg, int err)
{
  if (err < 0) {
    err = -err;
  }
  fprintf(stderr, "%s: %s\n", msg, strerror(err));
  exit(1);
}

static inline void die(const char *msg)
{
  fprintf(stderr, "%s\n", msg);
  exit(1);
}
