#include <linux/bpf.h>
#include "bpf_helpers.h"
#include "common.h"

char _license[] SEC("license") = "GPL";

int main() {

  bpf_debug("IN %d\n", 4);

  return bpf_redirect(2, 0);

}
