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

folly_rev=$(sed 's/Subproject commit //' "$start_dir"/../build/deps/github_hashes/facebook/folly-rev.txt)
wangle_rev=$(sed 's/Subproject commit //' "$start_dir"/../build/deps/github_hashes/facebook/wangle-rev.txt)

# Must execute from the directory containing this script
cd "$(dirname "$0")"

# RedHet/CentOS

sudo yum -y install autoconf268 automake libtool cmake autoconf-archive gcc-c++ boost-devel
sudo yum -y install devtoolset-2-gcc-c++ devtoolset-2-binutils-devel
export CXX=/opt/rh/devtoolset-2/root/usr/bin/g++

if [ ! -e google-glog ]; then
    echo "fetching glog from github.com"
    git clone git@github.com:google/glog.git google-glog
    cd google-glog
    ./configure
    make
    sudo make install
    cd ..
fi

if [ ! -e gflags-gflags ]; then
    echo "fetching gflags from github.com"
    git clone git@github.com:gflags/gflags gflags-gflags
    mkdir build && cd build
    ccmake ..
    make
    sudo make install
    cd ..
fi

if [ ! -e double-conversion ]; then
    echo "Fetching double-conversion from git..."
    git clone https://github.com/floitsch/double-conversion.git double-conversion
    cd double-conversion
    cmake . -DBUILD_SHARED_LIBS=ON
    sudo make install
    cd ..
fi


# Get folly
if [ ! -e folly/folly ]; then
    echo "Cloning folly"
    git clone https://github.com/facebook/folly
fi
cd folly/folly
git fetch
git checkout "$folly_rev"

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
git checkout "$wangle_rev"

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
