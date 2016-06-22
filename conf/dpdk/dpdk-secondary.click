// Run: <path-to-click> -c 4 -n 4 --proc-type secondary -v -- conf/dpdk-secondary.click

define(
        $queueSize 1024,
        $burst     32,
);

// Rx ring from Main (primary) to NF1 (secondary)
nf1_from_main :: FromDPDKRing(MEM_POOL 1, FROM_PROC nf1_rx, TO_PROC main_tx, BURST $burst);
// Tx ring from NF1 (secondary) back to Main (primary)
nf1_to_main   :: ToDPDKRing  (MEM_POOL 2, FROM_PROC nf1_tx, TO_PROC main_rx, IQUEUE $queueSize, BURST $burst);

nf1_from_main
//      -> Strip(14)
//      -> MarkIPHeader()
//      -> IPPrint(NF1, TTL true, LENGTH true)
//      -> Unstrip(14)
        -> nf1_to_main;
