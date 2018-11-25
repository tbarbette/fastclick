// Run: sudo bin/click --dpdk -c 0x4 -n 4 --proc-type secondary -v -- conf/dpdk/dpdk-ring-secondary.click

define(
	$queueSize 1024
);

// Rx ring from Main (primary) to NF1 (secondary)
nf1_from_main :: FromDPDKRing(MEM_POOL 1, FROM_PROC nf1_rx, TO_PROC main_tx);
// Tx ring from NF1 (secondary) back to Main (primary)
nf1_to_main   :: ToDPDKRing  (MEM_POOL 2, FROM_PROC nf1_tx, TO_PROC main_rx, IQUEUE $queueSize);

nf1_from_main
	-> Print(NF1)
	-> nf1_to_main;
