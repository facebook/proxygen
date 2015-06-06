#!/bin/bash

## Run this script to build proxygen and run the tests. If you want to
## install proxygen to use in another C++ project on this machine, run
## the sibling file `reinstall.sh`.

if [ -z $1 ]; 
  then jobs='-j8'; 
  else jobs=$1; 
fi

set -e
start_dir=`pwd`
trap "cd $start_dir" EXIT

# Must execute from the directory containing this script
cd "$(dirname "$0")"

# Some extra dependencies for Ubuntu 13.10 and 14.04
sudo apt-get install -yq \
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
    wget

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


if [ ! -e folly/folly ]; then
    echo "Cloning folly"
    git clone https://github.com/facebook/folly
fi
cd folly/folly
git fetch
git checkout v0.42.0

# Build folly
autoreconf --install
./configure
make $jobs

if test $? -ne 0; then
	echo "fatal: folly build failed"
	exit -1
fi

# Build proxygen
cd ../..
autoreconf -ivf
CPPFLAGS=" -I`pwd`/folly/" LDFLAGS="-L`pwd`/folly/folly/.libs/" ./configure
make $jobs

# Run tests
make check
