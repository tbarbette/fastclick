from mininet.topo import Topo
from mininet.net import Mininet
from mininet.util import dumpNodeConnections
from mininet.cli import CLI

import sys
import time


class MyTopo(Topo):
    def build(self):
        self.h1 = self.addHost("h1")
        self.h2 = self.addHost("h2")
        self.sw1 = self.addHost("sw1", ip="babe:1::6/64")
        self.sw2 = self.addHost("sw2", ip="babe:1::8/64")
        self.addLink(self.h1, self.sw1)
        self.addLink(self.h2, self.sw2)
        self.addLink(self.sw1, self.sw2)


def simpleRun():
    topo = MyTopo()
    net = Mininet(topo)
    net.start()

    dumpNodeConnections(net.hosts)

    # Add default routes to see the packets
    net["h1"].cmd("ip -6 route add default dev h1-eth0")
    net["h2"].cmd("ip -6 route add default dev h2-eth0")

    # Add IPv6 addresses to h1 and h2
    net["h1"].cmd("ifconfig h1-eth0 add babe:1::5/64")
    net["h2"].cmd("ifconfig h2-eth0 add babe:2::5/64")

    # Add IPv6 addresses to sw1 and sw2 and their hosts
    net["sw1"].cmd("ifconfig sw1-eth0 add babe:1::6/64")
    net["sw2"].cmd("ifconfig sw2-eth0 add babe:2::6/64")

    # Add IP addresses to sw1 and sw2 together
    net["sw1"].cmd("ifconfig sw1-eth1 add babe:3::1/64")
    net["sw2"].cmd("ifconfig sw2-eth1 add babe:3::2/64")

    # Add intermediate IPv6 addresses to test IPv6 Segment Routing
    # TODO: replace by the SIDs when SRv6 works in Click
    net["sw1"].cmd("ifconfig sw1-eth0 add fc00::a/64")
    net["sw2"].cmd("ifconfig sw2-eth1 add fc00::9/64")

    # Add an IPv6 Segment Routing Header to the packets from h1
    # Inline insertion with an intermediate segment
    # Packet will visit: fc00::a -> fc00::9 -> babe:2::5
    net["h1"].cmd("ip -6 route add babe:2::5/64 encap seg6 mode inline segs fc00::a,fc00::9 dev h1-eth0")
    
    
    CLI(net)
    net.stop()


if __name__ == "__main__":
    simpleRun()