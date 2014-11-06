#!/bin/bash

## Run this script to build proxygen and run the tests. If you want to
## install proxygen to use in another C++ project on this machine, run
## the sibling file `reinstall.sh`.

set -e
start_dir=`pwd`
trap "cd $start_dir" EXIT

# Must execute from the directory containing this script
cd "$(dirname "$0")"

# Some extra dependencies for Ubuntu 13.10 and 14.04
sudo apt-get install \
    flex \
    bison \
    libkrb5-dev \
    libsasl2-dev \
    libnuma-dev \
    pkg-config \
    libssl-dev \
    libcap-dev \
    ruby \
    gperf \
    autoconf-archive \
    libevent-dev \
    libgoogle-glog-dev \
    wget

git clone https://github.com/facebook/fbthrift || true
cd fbthrift/thrift

# Rebase in case we've already downloaded thrift and folly
git fetch && git rebase origin/master
if [ -e folly/folly ]; then
    # We have folly already downloaded
    cd folly/folly
    git fetch && git rebase origin/master
    cd ../..
fi

# Build folly and fbthrift
./deps.sh

# Build proxygen
cd ../..
autoreconf -ivf
CPPFLAGS=" -I`pwd`/fbthrift/thrift/folly/ -I`pwd`/fbthrift/" \
    LDFLAGS="-L`pwd`/fbthrift/thrift/lib/cpp/.libs/ -L`pwd`/fbthrift/thrift/lib/cpp2/.libs/ -L`pwd`/fbthrift/thrift/folly/folly/.libs/" ./configure
make -j8

# Run tests
make check
