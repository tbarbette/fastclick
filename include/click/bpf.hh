#ifndef CLICK_BPF_H
#define CLICK_BPF_H

#ifndef HAVE_BPF_STATS_TYPE
    enum bpf_stats_type {
            /* enabled run_time_ns and run_cnt */
            BPF_STATS_RUN_TIME = 0,
    };
#endif
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

// libbpf 0.8 added bpf_xdp_attach(), libbpf 1.0 removed bpf_prog_load_xattr()
#if defined(LIBBPF_MAJOR_VERSION) && (LIBBPF_MAJOR_VERSION > 0 || LIBBPF_MINOR_VERSION >= 8)
# define HAVE_LIBBPF_NEW_API 1
#endif

#endif
