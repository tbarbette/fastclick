define(
	$macAddr0       ae:aa:aa:79:fe:58,
	$ipAddr0        10.0.0.1,
	$ipNetHost0     10.0.0.0/32,
	$ipBcast0       10.0.0.255/32,
	$ipNet0         10.0.0.0/24,

	$macAddr1       ae:aa:aa:24:ef:2a,
	$ipAddr1        20.0.0.1,
	$ipNetHost1     20.0.0.0/32,
	$ipBcast1       20.0.0.255/32,
	$ipNet1         20.0.0.0/24,

	$gwMACAddr0     ae:aa:aa:49:19:da,
	$gwMACAddr1     ae:aa:aa:7c:4b:ce,

	$queueSize      1024,
	$mtuSize        9000,
);

require(library dummy_firewall_in.click)
require(library dummy_dpi.click)

elementclass FNT{
 input
 -> MarkMACHeader()
 -> fw :: Firewall()
 -> dpi :: DPI()
 -> output
}


