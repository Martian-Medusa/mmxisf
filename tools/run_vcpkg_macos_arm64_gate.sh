#!/bin/sh

set -eu

script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository_root=$(CDPATH= cd -- "$script_directory/.." && pwd)
vcpkg_root=${MMXISF_VCPKG_ROOT:-}
build_root=${MMXISF_VCPKG_BUILD_ROOT:-"$repository_root/build-vcpkg-production"}
parallel_jobs=${MMXISF_VCPKG_JOBS:-2}
warning_flags=${MMXISF_VCPKG_CXX_FLAGS:-"-Wall -Wextra -Wpedantic -Werror"}

if [ "$(uname -s)" != Darwin ] || [ "$(uname -m)" != arm64 ]; then
  printf '%s\n' "This gate requires macOS arm64" >&2
  exit 2
fi

if [ -z "$vcpkg_root" ] ||
   [ ! -f "$vcpkg_root/scripts/buildsystems/vcpkg.cmake" ]; then
  printf '%s\n' \
    "Set MMXISF_VCPKG_ROOT to a bootstrapped official vcpkg checkout" >&2
  exit 2
fi

toolchain="$vcpkg_root/scripts/buildsystems/vcpkg.cmake"
viewer_output="$build_root/artifacts"
install_directory="$build_root/install"
consumer_directory="$build_root/consumer"

cmake -S "$repository_root" -B "$build_root" \
  "-DCMAKE_TOOLCHAIN_FILE=$toolchain" \
  -DMMXISF_ENFORCE_PRODUCTION_DEPENDENCY_BASELINE=ON \
  -DMMXISF_BUILD_TESTS=ON \
  -DMMXISF_BUILD_TOOLS=ON \
  -DMMXISF_BUILD_VIEWER=ON \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_CXX_FLAGS=$warning_flags" \
  "-DMMXISF_VIEWER_OUTPUT_DIR=$viewer_output"
cmake --build "$build_root" --parallel "$parallel_jobs"
ctest --test-dir "$build_root" --output-on-failure

cmake --install "$build_root" --prefix "$install_directory"
vcpkg_prefix="$build_root/vcpkg_installed/arm64-osx"
cmake -S "$repository_root/tests/package_consumer" \
  -B "$consumer_directory" \
  "-DCMAKE_PREFIX_PATH=$install_directory;$vcpkg_prefix" \
  -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_CXX_FLAGS=$warning_flags"
cmake --build "$consumer_directory" --parallel "$parallel_jobs"
ctest --test-dir "$consumer_directory" --output-on-failure

viewer_bundle="$viewer_output/mmXISF Viewer PoC.app"
viewer_executable="$viewer_bundle/Contents/MacOS/mmXISF Viewer PoC"
codesign --verify --deep --strict --verbose=2 "$viewer_bundle"

if ! file "$viewer_executable" | awk '/Mach-O 64-bit executable arm64/ {ok=1} END {exit !ok}'; then
  printf '%s\n' "Viewer is not a macOS arm64 executable" >&2
  exit 1
fi

if ! otool -L "$viewer_executable" | awk '
  NR > 1 && $1 !~ /^\/System\// && $1 !~ /^\/usr\/lib\// {
    print "Unexpected non-system linkage: " $1 > "/dev/stderr"
    bad = 1
  }
  END { exit bad }
'; then
  exit 1
fi

test -s "$build_root/binary-dependencies.spdx.json"
printf '%s\n' "mmxisf vcpkg macOS arm64 production-baseline gate: PASS"
