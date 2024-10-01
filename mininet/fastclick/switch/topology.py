#!/usr/bin/python
#This is a python (Mininet) script that will generate a topology
#with 2 hosts interconnected by a switch, but the switch runs using FastClick

from mininet.topo import Topo
from mininet.net import Mininet
from mininet.util import dumpNodeConnections
from mininet.log import setLogLevel
from mininet.cli import CLI

import sys
import time

class MyTopo(Topo):

    "Single switch connected to 2 hosts."
    def build(self):

        self.h1 = self.addHost('h1', ip="10.220.0.5/16")
        self.h2 = self.addHost('h2', ip="10.220.0.10/16")
        self.sw1 = self.addHost('sw1')
        self.addLink(self.h1, self.sw1)
        self.addLink(self.h2, self.sw1)


def simpleTest():
    "Create and test a simple network"
    topo = MyTopo()
    net = Mininet(topo)
    net.start()

    print("Dumping host connections")
    dumpNodeConnections(net.hosts)

    sw1 = net.get(topo.sw1)

    cmd = "cd /home/vagrant/fastclick && ./bin/click /home/vagrant/fastclick/conf/switch/switch-2ports-vanilla.click "+ ' '.join(sys.argv[1:])+" 2>&1 | tee click.log &"
    print("Launching FastClick with " + cmd)
    sw1.cmd(cmd)


    print("Waiting for FastClick to set up...")
    time.sleep(5)

    client = net.get("h1")
    print("Waiting for everything to set up...")
    time.sleep(5)

    print("Verifying connectivity")
    client.sendCmd("ping -c 1 10.220.0.10")
    result = client.waitOutput()
    print(result)

    CLI(net)
    net.stop()

if __name__ == '__main__':
    # Tell mininet to print useful information
    setLogLevel('info')
    simpleTest()
