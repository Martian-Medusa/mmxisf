#!/bin/sh

set -eu

script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository_root=$(CDPATH= cd -- "$script_directory/.." && pwd)
vcpkg_root=${MMXISF_VCPKG_ROOT:-}
image=${MMXISF_LINUX_DOCKER_IMAGE:-mmxisf-ci:ubuntu-24.04-amd64}
gate_directory_name=${MMXISF_VCPKG_GATE_DIRECTORY:-build-vcpkg-linux-amd64}
parallel_jobs=${MMXISF_VCPKG_JOBS:-2}

if [ -z "$vcpkg_root" ] || [ ! -d "$vcpkg_root/.git" ]; then
  printf '%s\n' \
    "Set MMXISF_VCPKG_ROOT to a dedicated official vcpkg checkout" >&2
  exit 2
fi

case "$gate_directory_name" in
  ""|/*|*..*)
    printf '%s\n' "MMXISF_VCPKG_GATE_DIRECTORY must be a safe relative directory name." >&2
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
  --volume "$vcpkg_root:/vcpkg" \
  --workdir /work \
  --env HOME=/tmp/mmxisf-vcpkg-home \
  --env MMXISF_VCPKG_ROOT=/vcpkg \
  --env "MMXISF_VCPKG_BUILD_ROOT=/work/$gate_directory_name" \
  --env "MMXISF_VCPKG_JOBS=$parallel_jobs" \
  --env VCPKG_DISABLE_METRICS=1 \
  "$image" \
  tools/run_vcpkg_linux_amd64_gate.sh
