#!/usr/bin/python
#This is a python (Mininet) script that will generate a topology and run MiddleClick
#Run me with "sudo python topology.py", after having installed mininet and middleclick. The best is
#to use the VM built using Vagrant available at []

from mininet.topo import Topo
from mininet.net import Mininet
from mininet.util import dumpNodeConnections
from mininet.log import setLogLevel
from mininet.cli import CLI

import sys
import time

class MyTopo(Topo):


    "Single switch connected to n hosts."
    def build(self, n=2):

        self.client = self.addHost('h1', ip="10.220.0.5/16", defaultRoute = "via 10.220.0.1")
        self.sw1 = self.addSwitch('sw1')
        self.addLink(self.client, self.sw1)

        self.mb1 = self.addHost('mb1')
        self.addLink(self.mb1, self.sw1, params1={'ip':"10.220.0.1/16"},addr1="98:03:9b:33:fe:e2")

        self.sw2 = self.addSwitch('sw2')
        self.addLink(self.mb1, self.sw2, params1={'ip':"10.221.0.1/16" },addr1="98:03:9b:33:fe:db")

        host = self.addHost('ws1', ip="10.221.0.5/16",  defaultRoute = "via 10.221.0.1")
        self.addLink(host, self.sw2)

    def buildConfigIDS(self, net):
        s = open('/conf/middleclick/middlebox-tcp-ids.click','r').read()

        #Nothing to replace

        return s

def simpleTest():
    "Create and test a simple network"
    topo = MyTopo(n=4)
    net = Mininet(topo)
    net.start()
    print "Dumping host connections"
    dumpNodeConnections(net.hosts)

    config = topo.buildConfigIDS(net)
    f = open('/home/vagrant/middleclick/tmp.config.click','wb')
    f.write(config)
    f.close()
    #print("Launching Cheetah with config:")
    #print(config)
    mb = net.get(topo.mb1)
    for i in mb.intfList():
        mb.cmd( 'ethtool -K %s gro off' % i )
    cmd = "cd /home/vagrant/middleclick && ./bin/click --dpdk -l 1-1 --vdev=eth_af_packet0,iface=" +mb.intfList()[0].name + " --vdev=eth_af_packet1,iface=" + mb.intfList()[1].name  + " -- tmp.config.click checksumoffload=0 word=nginx.com "+ ' '.join(sys.argv[1:])+" &> click.log &"
    print("Launching MiddleClick with " + cmd)
    mb.cmd(cmd)


    print("Waiting for MB to set up...")
    time.sleep(3)

    msrv = net.getNodeByName("ws1")
    msrv.cmd("sudo nginx")

    client = net.get("h1")
    print("Waiting for everything to set up...")
    time.sleep(2)

    print("Verifying connectivity")
    client.sendCmd("timeout 2 wget -o /dev/null -O /dev/stdout http://10.220.0.1/")
    result = client.waitOutput()
    print(result)
    CLI(net)
    net.stop()

if __name__ == '__main__':
    # Tell mininet to print useful information
    setLogLevel('info')
    simpleTest()
