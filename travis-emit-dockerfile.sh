#!/bin/bash -ex
# .travis.yml explains why this is a separate script.
proxygen_git_hash=$(git rev-parse HEAD) ||
  echo "Not in a git repo? Defaulting to building proxygen master."
./emit-dockerfile.py \
  --ubuntu-version "$ubuntu_version" \
  --gcc-version "$gcc_version" \
  --make-parallelism 4 \
  --substitute proxygen_git_hash "$(printf %q "${proxygen_git_hash:-master}")"
