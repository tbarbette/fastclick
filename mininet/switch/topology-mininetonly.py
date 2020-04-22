#!/usr/bin/python
#This is a python (Mininet) script that will generate a simple h1-sw1-h2 two-hosts topology

from mininet.topo import Topo
from mininet.net import Mininet
from mininet.util import dumpNodeConnections
from mininet.log import setLogLevel
from mininet.cli import CLI

import sys
import time

class MyTopo(Topo):


    "Single switch connected to n hosts."
    def build(self):

        self.h1 = self.addHost('h1', ip="10.220.0.5/16")
        self.h2 = self.addHost('h2', ip="10.220.0.10/16")
        self.sw1 = self.addSwitch('sw1')
        self.addLink(self.h1, self.sw1)
        self.addLink(self.h2, self.sw1)

def simpleTest():
    "Create and test a simple network"
    topo = MyTopo()
    net = Mininet(topo)
    net.start()

    print "Dumping host connections"
    dumpNodeConnections(net.hosts)

    CLI(net)
    net.stop()

if __name__ == '__main__':
    # Tell mininet to print useful information
    setLogLevel('info')
    simpleTest()
