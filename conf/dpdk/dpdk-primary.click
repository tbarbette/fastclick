// Run: <path-to-click> -c 55 -n 4 --proc-type primary -v -- conf/dpdk-primary.click

define(
        $iface0    0,
        $iface1    1,
        $queueSize 1024,
        $burst     32,
);

// Module's I/O
nicIn0  :: FromDPDKDevice($iface0, BURST $burst);
nicOut0 :: ToDPDKDevice  ($iface0, IQUEUE $queueSize, BURST $burst);

nicIn1  :: FromDPDKDevice($iface1, BURST $burst);
nicOut1 :: ToDPDKDevice  ($iface1, IQUEUE $queueSize, BURST $burst);

// Tx ring from Main (primary) to NF1 (secondary)
main_to_nf1   :: ToDPDKRing  (MEM_POOL 1, FROM_PROC main_tx, TO_PROC nf1_rx, IQUEUE $queueSize, BURST $burst);
// Rx ring from NF1 (secondary) back to Main (primary)
main_from_nf1 :: FromDPDKRing(MEM_POOL 2, FROM_PROC main_rx, TO_PROC nf1_tx, BURST $burst);

// NIC 0 --> Main --> NF1
nicIn0
//      -> Strip(14)
//      -> MarkIPHeader()
//      -> IPPrint("Before-NF1", TTL true, LENGTH true)
//      -> Unstrip(14)
        -> main_to_nf1;

// ... NF1 --> Main --> NIC 1
main_from_nf1
//      -> Strip(14)
//      -> MarkIPHeader()
//      -> IPPrint(" After-NF1", TTL true, LENGTH true)
//      -> Unstrip(14)
        -> nicOut1;

Idle
        -> nicOut0;
nicIn1
        -> Discard;