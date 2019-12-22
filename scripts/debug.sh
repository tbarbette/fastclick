#!/bin/bash

if [[ $UID -ne 0 ]]; then
        echo "must be root"
        exit 1
fi

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
srcdir="$DIR/.."
moadir="$srcdir/../../.."

export LD_LIBRARY_PATH=/usr/local/lib64

./xdpoff.sh

ip link set up dev ens1f0
ip link set up dev ens1f1
ip link set promisc on dev ens1f0
ip link set promisc on dev ens1f1

gdb --args $srcdir/userlevel/click \
        $moadir/test/click/xdp-impared-to-from.click

