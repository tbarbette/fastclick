from mininet.topo import Topo
from mininet.net import Mininet
from mininet.util import dumpNodeConnections
from mininet.cli import CLI

import sys
import time


class MyTopo(Topo):
    def build(self):
        self.h1 = self.addHost("h1", ip="babe:1::5/64")
        self.h2 = self.addHost("h2", ip="babe:1::10/64")
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
    
    CLI(net)
    net.stop()


if __name__ == "__main__":
    simpleRun()