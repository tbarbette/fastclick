/*
 * This is a L3 version of tester. Port 0 and 1 are expected to be attached to the
 * two interface of your DUT.
 * Port 0 will only generate packets while port 1 expects to receive packets back.
 * Both ports will respond to ARP queries.
 * 
 * Author: Tom Barbette <barbette@kth.se>, Lida Liu <lidal@kth.se>
 */

define($L 60, $N 100, $S 100000);
define($block true);
define($ring_size 0); //virtio needs a direct assignation of 512, use 0 for automatic
define($dstmac 52:54:00:1e:4f:f3);

define($srcmac 52:54:00:ef:81:e0);

AddressInfo(
    lan_interface    192.168.100.166     52:54:00:ef:81:e0,
    wan_interface    192.168.200.219     52:54:00:50:07:4b
);

DPDKInfo(65536);

replay0 :: ReplayUnqueue(STOP $S, QUICK_CLONE 0);

td0 :: ToDPDKDevice(0, NDESC $ring_size, BLOCKING $block);
td1 :: ToDPDKDevice(1, NDESC $ring_size, BLOCKING $block);

class_left  :: Classifier(12/0806 20/0001, -);
class_right :: Classifier(12/0806 20/0001, -);

is0 :: FastUDPFlows(RATE 0, LIMIT $N, LENGTH $L, SRCETH 52:54:00:ef:81:e0, DSTETH $dstmac, SRCIP 192.168.100.166, DSTIP 192.168.200.219, FLOWS 1, FLOWSIZE $N);

StaticThreadSched(replay0 0);

is0 -> MarkMACHeader
-> EnsureDPDKBuffer
-> replay0
-> ic0 :: AverageCounter()
-> td0;

fd0 :: FromDPDKDevice(0, NDESC 512) -> class_left;
fd1 :: FromDPDKDevice(1, NDESC 512) -> class_right;


class_left[0] -> ARPResponder(lan_interface) -> td0;     //ARP Requests
class_left[1] -> oc0 :: AverageCounter() -> Discard;     // Others Counting

class_right[0] -> ARPResponder(wan_interface) -> td1;     //ARP Requests
class_right[1] -> oc1 :: AverageCounter() -> Discard;     // Others Counting


DriverManager(wait, wait 10ms, print $(ic0.count), print $(oc1.count), print $(ic0.link_rate), print $(oc1.link_rate));
