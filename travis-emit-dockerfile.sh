#!/bin/bash
# .travis.yml explains why this is a separate script.
proxygen_git_hash=$(git rev-parse HEAD)
./emit-dockerfile.py \
  --ubuntu-version "$ubuntu_version" \
  --gcc-version "$gcc_version" \
  --make-parallelism 4 \
  --substitute proxygen_git_hash "$(printf %q "${proxygen_git_hash:-master}")"
