from mininet.topo import Topo
from mininet.net import Mininet
from mininet.util import dumpNodeConnections
from mininet.cli import CLI
from mininet.link import TCLink
import subprocess

import sys
import time


class MyTopo(Topo):
    def build(self):
        self.h1 = self.addHost("h1",  mac='00:00:00:00:00:01')
        self.h2 = self.addHost("h2",  mac='00:00:00:00:00:04')
        self.sw1 = self.addHost("sw1", ip="babe:1::6/64",  mac='00:00:00:00:00:02')
        self.sw2 = self.addHost("sw2", ip="babe:2::8/64",  mac='00:00:00:00:00:03')
        self.addLink(self.h1, self.sw1)
        self.addLink(self.h2, self.sw2)
        self.addLink(self.sw1, self.sw2, cls=TCLink, delay="10ms", bw=300)


def run_cli(net):
    CLI(net)


def simpleRun():
    topo = MyTopo()
    net = Mininet(topo)
    net.start()

    dumpNodeConnections(net.hosts)

    # Add default routes to see the packets
    net["h1"].cmd("ip -6 route add default dev h1-eth0")
    net["sw1"].cmd("ip -6 route add default dev sw1-eth1")
    net["h2"].cmd("ip -6 route add default dev h2-eth0")

    # Add IPv6 addresses to h1 and h2
    net["h1"].cmd("ifconfig h1-eth0 add babe:1::5/64")
    net["h2"].cmd("ifconfig h2-eth0 add babe:2::5/64")

    # Add IPv6 addresses to sw1 and sw2 and their hosts
    net["sw1"].cmd("ifconfig sw1-eth0 add babe:1::6/64")
    net["sw2"].cmd("ifconfig sw2-eth0 add babe:2::8/64")

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

    # net["h1"].cmd("ip -6 route add babe:2::5/64 encap seg6 mode inline segs fc00::a,fc00::9 dev h1-eth0")

    # Enable SRv6
    net["h1"].cmd("sysctl net.ipv6.conf.all.seg6_enabled=1")
    net["h1"].cmd("sysctl net.ipv6.conf.default.seg6_enabled=1")
    net["h1"].cmd("sysctl net.ipv6.conf.h1-eth0.seg6_enabled=1")
    net["h2"].cmd("sysctl net.ipv6.conf.all.seg6_enabled=1")
    net["h2"].cmd("sysctl net.ipv6.conf.default.seg6_enabled=1")
    net["h2"].cmd("sysctl net.ipv6.conf.h2-eth0.seg6_enabled=1")

    net["h1"].cmd("ethtool -K tx off")
    net["sw1"].cmd("ethtool -K tx off")
    net["sw2"].cmd("ethtool -K tx off")
    net["h2"].cmd("ethtool -K tx off")

    net["h1"].cmd("ethtool --offload h1-eth0 rx off tx off")

    net["sw1"].cmd("ethtool --offload sw1-eth1 rx off tx off")
    net["sw1"].cmd("ethtool --offload sw1-eth0 rx off tx off")

    net["sw2"].cmd("ethtool --offload sw2-eth1 rx off tx off")
    net["sw2"].cmd("ethtool --offload sw2-eth0 rx off tx off")

    net["h2"].cmd("ethtool --offload h2-eth0 rx off tx off")

    MTU = 1300
    net["h1"].cmd("ifconfig h1-eth0 mtu {}".format(MTU))
    #net["h2"].cmd("ifconfig h2-eth0 mtu {}".format(MTU))

    net["sw1"].cmd("ifconfig sw1-eth0 mtu {}".format(MTU))
    #net["sw1"].cmd("ifconfig sw1-eth1 mtu {}".format(MTU))

    #net["sw2"].cmd("ifconfig sw2-eth0 mtu {}".format(MTU))
    #net["sw2"].cmd("ifconfig sw2-eth1 mtu {}".format(MTU))

    return net

    # net.stop()


if __name__ == "__main__":
    net = simpleRun()
    net.stop()
