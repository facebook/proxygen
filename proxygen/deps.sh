#!/bin/bash

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
    libgoogle-glog-dev

git clone https://github.com/facebook/fbthrift || true
cd fbthrift/thrift

# Rebase and uninstall in case we've already downloaded thrift and folly
git fetch && git rebase origin/master
sudo make uninstall || true
if [ -e folly/folly ]; then
    # We have folly already downloaded
    cd folly/folly
    git fetch && git rebase origin/master
    sudo make uninstall || true
    cd ../..
fi

# Build folly and fbthrift
./deps.sh

# Install folly
cd folly/folly
sudo make install

# Install fbthrift
cd ../..
sudo make install

# Build proxygen
sudo /sbin/ldconfig
cd ../..
autoreconf -ivf
./configure
make -j8

# Run tests
make check

# Install
sudo make install

sudo /sbin/ldconfig
