#ifndef CLICK_BPF_H
#define CLICK_BPF_H

#ifndef bpf_stats_type
    enum bpf_stats_type {
            /* enabled run_time_ns and run_cnt */
            BPF_STATS_RUN_TIME = 0,
    };
#endif
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#endif
