/*
 * This is a L3 version of tester. Port 0 and 1 are expected to be attached to the
 * two interface of your DUT.
 * Port 0 will only have the function to generate the packets while port 1 will receive
 * and dealing with the ARP request.
 * 
 * Author: Lida Liu <lidal@kth.se>
 */

define($L 60, $N 100, $S 100000);
define($block true);
define($dstmac 52:54:00:1e:4f:f3);

define($srcmac 52:54:00:ef:81:e0);

AddressInfo(
    lan_interface    192.168.100.166     52:54:00:ef:81:e0,
    wan_interface    192.168.200.219     52:54:00:50:07:4b
);

replay0 :: ReplayUnqueue(STOP $S, QUICK_CLONE 0);

nicOut0 :: ToDPDKDevice(0, NDESC 512, BLOCKING $block); //virtio requires ring of size 512.
nicOut1 :: ToDPDKDevice(1, NDESC 512);

DPDKInfo(65536);

class_right :: Classifier(12/0806 20/0001, -);

is0 :: FastUDPFlows(RATE 0, LIMIT $N, LENGTH $L, SRCETH 52:54:00:ef:81:e0, DSTETH $dstmac, SRCIP 192.168.100.166, DSTIP 192.168.200.219, FLOWS 1, FLOWSIZE $N);

StaticThreadSched(is0 0);
StaticThreadSched(replay0 1);

is0 -> MarkMACHeader
-> EnsureDPDKBuffer
-> replay0
-> ic0 :: AverageCounter()
-> nicOut0;

fd0 :: FromDPDKDevice(0, NDESC 512) -> oc0 :: AverageCounter() -> Discard;
fd1 :: FromDPDKDevice(1, NDESC 512) -> class_right;

class_right[0] -> ARPResponder(wan_interface) -> nicOut1; //ARP Requests
class_right[1] -> oc1 :: AverageCounter() -> Discard;     // Others Counting


DriverManager(wait, wait, wait 10ms, print $(ic0.count),  print $(oc1.count), print $(ic0.link_rate), print $(oc1.link_rate));