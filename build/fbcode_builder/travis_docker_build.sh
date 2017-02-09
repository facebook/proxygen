#!/bin/bash -ex
# .travis.yml in the top-level dir explains why this is a separate script.
# Read the docs: ./make_docker_context.py --help
os_image=${os_image?Must be set by Travis}
gcc_version=${gcc_version?Must be set by Travis}
make_parallelism=${make_parallelism:-4}
cur_dir="$(readlink -f "$(dirname "$0")")"
docker_context_dir=$(
  cd "$cur_dir/.."  # Let the script find our fbcode_builder_config.py
  "$cur_dir/make_docker_context.py" \
    --os-image "$os_image" \
    --gcc-version "$gcc_version" \
    --make-parallelism "$make_parallelism" \
    --local-repo-dir "$cur_dir/../.."
)
cd "${docker_context_dir?Failed to make Docker context directory}"
docker build .
