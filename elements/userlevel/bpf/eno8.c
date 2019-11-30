#include <linux/bpf.h>
#include "bpf_helpers.h"
#include "common.h"

char _license[] SEC("license") = "GPL";

int main() {

  bpf_debug("IN eno8\n");

  return bpf_redirect(5, 0);

}
