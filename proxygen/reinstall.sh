#!/bin/bash

## Run this script to (re)install proxygen and its dependencies (fbthrift
## and folly). You must have first run compiled all the dependencies. This
## is usually done by running `deps.sh`.

set -e
start_dir=`pwd`
trap "cd $start_dir" EXIT

# Must execute from the directory containing this script
cd "$(dirname "$0")"

# Install folly
cd fbthrift/thrift/folly/folly
sudo make uninstall
sudo make install

# Install fbthrift
cd ../..
sudo make uninstall
sudo make install

# Install proxygen
cd ../..
sudo make uninstall
sudo make install
