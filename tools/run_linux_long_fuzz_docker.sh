#!/bin/sh

set -eu

script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository_root=$(CDPATH= cd -- "$script_directory/.." && pwd)
image=${MMXISF_LINUX_DOCKER_IMAGE:-mmxisf-ci:ubuntu-24.04-amd64}
campaign_directory_name=${MMXISF_LINUX_FUZZ_DIRECTORY:-build-linux-long-fuzz}
duration_seconds=${MMXISF_FUZZ_DURATION_SECONDS:-900}
parallel_jobs=${MMXISF_FUZZ_BUILD_JOBS:-2}

case "$campaign_directory_name" in
  ""|/*|*..*)
    printf '%s\n' \
      "MMXISF_LINUX_FUZZ_DIRECTORY must be a safe relative directory name" >&2
    exit 2
    ;;
esac

docker build \
  --file "$repository_root/containers/ubuntu-24.04-ci.Dockerfile" \
  --tag "$image" \
  "$repository_root"

docker run --rm --init --platform linux/amd64 \
  --user "$(id -u):$(id -g)" \
  --volume "$repository_root:/work" \
  --workdir /work \
  --env HOME=/tmp/mmxisf-fuzz-home \
  --env ASAN_SYMBOLIZER_PATH=/usr/bin/llvm-symbolizer-18 \
  --env MMXISF_FUZZ_CC=clang \
  --env MMXISF_FUZZ_CXX=clang++ \
  --env "MMXISF_FUZZ_CAMPAIGN_ROOT=/work/$campaign_directory_name" \
  --env "MMXISF_FUZZ_DURATION_SECONDS=$duration_seconds" \
  --env "MMXISF_FUZZ_BUILD_JOBS=$parallel_jobs" \
  "$image" \
  tools/run_long_fuzz_campaign.sh
