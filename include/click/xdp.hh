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

typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
