#!/usr/bin/env bash

## Run this script to (re)install proxygen and its dependencies (fbthrift
## and folly). You must first compile all the dependencies before running this. This
## Usually this is done by first running `deps.sh`.

set -e
start_dir=$(pwd)
trap 'cd $start_dir' EXIT

# Must execute from the directory containing this script
cd "$(dirname "$0")"

# Install folly
cd folly/_build
if [ -f install_manifest.txt ]; then
  sudo xargs rm -f -- < install_manifest.txt
fi
sudo make install

# Install proxygen
cd ../..
sudo make uninstall
sudo make install

# Make sure the libraries are available
sudo /sbin/ldconfig
