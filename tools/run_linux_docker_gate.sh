#!/bin/sh

set -eu

script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository_root=$(CDPATH= cd -- "$script_directory/.." && pwd)
image=${MMXISF_LINUX_DOCKER_IMAGE:-mmxisf-ci:ubuntu-24.04-amd64}
gate_directory_name=${MMXISF_LINUX_DOCKER_GATE_DIRECTORY:-build-linux-docker-gates}
parallel_jobs=${MMXISF_LINUX_DOCKER_JOBS:-2}
warning_flags="-Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers"

case "$gate_directory_name" in
  ""|/*|*..*)
    printf '%s\n' "MMXISF_LINUX_DOCKER_GATE_DIRECTORY must be a safe relative directory name." >&2
    exit 2
    ;;
esac

docker build \
  --file "$repository_root/containers/ubuntu-24.04-ci.Dockerfile" \
  --tag "$image" \
  "$repository_root"

docker run --rm --init --platform linux/amd64 \
  --volume "$repository_root:/work" \
  --workdir /work \
  --env "MMXISF_LOCAL_GATE_ROOT=/work/$gate_directory_name" \
  --env "MMXISF_LOCAL_JOBS=$parallel_jobs" \
  --env "MMXISF_LOCAL_CXX_FLAGS=$warning_flags" \
  --env "MMXISF_LOCAL_SANITIZER_CC=clang" \
  --env "MMXISF_LOCAL_SANITIZER_CXX=clang++" \
  "$image" \
  tools/run_local_quality_gates.sh
