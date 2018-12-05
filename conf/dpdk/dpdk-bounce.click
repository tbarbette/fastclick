/**
 * This simple configuration bounces packets from a single interface to itself.
 * MAC addresses are inverted
 *
 * A minimal launch line would be :
 * sudo bin/click --dpdk -- conf/dpdk/dpdk-bounce.click
 */

define ($print true)

FromDPDKDevice(0)
	-> EtherMirror()
	-> Print(ACTIVE $print)
	-> ToDPDKDevice(0)
