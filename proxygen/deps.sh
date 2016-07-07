#!/bin/bash

## Run this script to build proxygen and run the tests. If you want to
## install proxygen to use in another C++ project on this machine, run
## the sibling file `reinstall.sh`.

# Parse args
JOBS=8
USAGE="./deps.sh [-j num_jobs]"
while [ "$1" != "" ]; do
  case $1 in
    -j | --jobs ) shift
                  JOBS=$1
                  ;;
    * )           echo $USAGE
                  exit 1
esac
shift
done

set -e
start_dir=`pwd`
trap "cd $start_dir" EXIT

# Must execute from the directory containing this script
cd "$(dirname "$0")"

# Some extra dependencies for Ubuntu 13.10 and 14.04
sudo apt-get install -yq \
    cmake \
    g++ \
    flex \
    bison \
    libkrb5-dev \
    libsasl2-dev \
    libnuma-dev \
    pkg-config \
    libssl-dev \
    libcap-dev \
    gperf \
    autoconf-archive \
    libevent-dev \
    libtool \
    libboost-all-dev \
    libjemalloc-dev \
    libsnappy-dev \
    wget \
    unzip

# Adding support for Ubuntu 12.04.x
# Needs libdouble-conversion-dev, google-gflags and double-conversion
# deps.sh in folly builds anyways (no trap there)
if ! sudo apt-get install -y libgoogle-glog-dev;
then
  if [ ! -e google-glog ]; then
    echo "fetching glog from svn (apt-get failed)"
    svn checkout https://google-glog.googlecode.com/svn/trunk/ google-glog
    cd google-glog
    ./configure
    make
    sudo make install
    cd ..
  fi
fi

if ! sudo apt-get install -y libgflags-dev;
then
  if [ ! -e google-gflags ]; then
    echo "Fetching gflags from svn (apt-get failed)"
    svn checkout https://google-gflags.googlecode.com/svn/trunk/ google-gflags
    cd google-gflags
    ./configure
    make
    sudo make install
    cd ..
  fi
fi

if  ! sudo apt-get install -y libdouble-conversion-dev;
then
  if [ ! -e double-conversion ]; then
    echo "Fetching double-conversion from git (apt-get failed)"
    git clone https://github.com/floitsch/double-conversion.git double-conversion
    cd double-conversion
    cmake . -DBUILD_SHARED_LIBS=ON
    sudo make install
    cd ..
  fi
fi


# Get folly
if [ ! -e folly/folly ]; then
    echo "Cloning folly"
    git clone https://github.com/facebook/folly
fi
cd folly/folly
git fetch
git checkout master

# Build folly
autoreconf --install
./configure
make -j$JOBS
sudo make install

if test $? -ne 0; then
  echo "fatal: folly build failed"
  exit -1
fi
cd ../..

# Get wangle
if [ ! -e wangle/wangle ]; then
    echo "Cloning wangle"
    git clone https://github.com/facebook/wangle
fi
cd wangle/wangle
git fetch
git checkout master

# Build wangle
cmake .
make -j$JOBS
sudo make install

if test $? -ne 0; then
  echo "fatal: wangle build failed"
  exit -1
fi
cd ../..

# Build proxygen
autoreconf -ivf
./configure
make -j$JOBS

# Run tests
LD_LIBRARY_PATH=/usr/local/lib make check

# Install the libs
sudo make install
