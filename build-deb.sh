#!/bin/bash

set -e

function usage() {
  echo "usage: build-deb.sh [options]"
  echo "options:"
  echo "  -c: build in a container"
  exit 1
}

while getopts "ch" opt; do
    case "$opt" in
    h)
      usage
      ;;
    c)
      containerize=true
      ;;
    esac
done

if [[ $containerize ]]; then
  docker run -v `pwd`:/code -w /code mergetb/kernel-builder /code/build-deb.sh
  exit $?
fi

curl https://pkg.mergetb.net/addrepo | RELEASE=kass bash -

export TOOL_ARGS="apt-get -o Debug::pkgProblemResolver=yes --no-install-recommends --yes"
mk-build-deps --install --tool="$TOOL_ARGS" debian/control

debuild -i -us -uc -b

mv ../fastclick*.build* .
mv ../fastclick*.changes .
mv ../fastclick*.deb .
