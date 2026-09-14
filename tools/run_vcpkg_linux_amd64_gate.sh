#!/bin/sh

set -eu

script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository_root=$(CDPATH= cd -- "$script_directory/.." && pwd)
vcpkg_root=${MMXISF_VCPKG_ROOT:-}
build_root=${MMXISF_VCPKG_BUILD_ROOT:-"$repository_root/build-vcpkg-linux-amd64"}
parallel_jobs=${MMXISF_VCPKG_JOBS:-2}
warning_flags=${MMXISF_VCPKG_CXX_FLAGS:-"-Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers"}

if [ "$(uname -s)" != Linux ] || [ "$(uname -m)" != x86_64 ]; then
  printf '%s\n' "This gate requires Linux amd64" >&2
  exit 2
fi

if [ -z "$vcpkg_root" ] ||
   [ ! -x "$vcpkg_root/vcpkg" ] ||
   [ ! -f "$vcpkg_root/scripts/buildsystems/vcpkg.cmake" ]; then
  printf '%s\n' \
    "Set MMXISF_VCPKG_ROOT to a Linux-bootstrapped official vcpkg checkout" >&2
  exit 2
fi

expected_baseline=$(sed -n \
  's/.*"builtin-baseline": "\([0-9a-f][0-9a-f]*\)".*/\1/p' \
  "$repository_root/vcpkg.json")
actual_baseline=$(git -C "$vcpkg_root" rev-parse HEAD)
if [ ${#expected_baseline} -ne 40 ] || [ "$actual_baseline" != "$expected_baseline" ]; then
  printf '%s\n' \
    "vcpkg checkout does not match the manifest builtin-baseline" >&2
  printf '%s\n' "expected: $expected_baseline" "actual:   $actual_baseline" >&2
  exit 2
fi

if [ -e "$build_root" ]; then
  printf '%s\n' "Refusing to reuse existing gate directory: $build_root" >&2
  exit 2
fi

toolchain="$vcpkg_root/scripts/buildsystems/vcpkg.cmake"
installed_directory="$build_root/vcpkg_installed"

configure_installed_consumer() {
  consumer_directory=$1
  install_directory=$2
  linkage=$3
  if [ "$linkage" = shared ]; then
    cmake --no-warn-unused-cli \
      -S "$repository_root/tests/package_consumer" \
      -B "$consumer_directory" \
      -G Ninja \
      "-DCMAKE_PREFIX_PATH=$install_directory" \
      "-DMMXISF_EXPECTED_PACKAGE_PREFIX=$install_directory" \
      -DMMXISF_EXPECTED_LINKAGE=shared \
      -DCMAKE_DISABLE_FIND_PACKAGE_EXPAT=TRUE \
      -DCMAKE_DISABLE_FIND_PACKAGE_LZ4=TRUE \
      -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=TRUE \
      -DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=TRUE \
      -DCMAKE_DISABLE_FIND_PACKAGE_ZSTD=TRUE \
      -DCMAKE_BUILD_TYPE=Release \
      "-DCMAKE_CXX_FLAGS=$warning_flags"
  else
    cmake -S "$repository_root/tests/package_consumer" \
      -B "$consumer_directory" \
      -G Ninja \
      "-DCMAKE_PREFIX_PATH=$install_directory;$installed_directory/x64-linux" \
      "-DMMXISF_EXPECTED_PACKAGE_PREFIX=$install_directory" \
      -DMMXISF_EXPECTED_LINKAGE=static \
      -DCMAKE_BUILD_TYPE=Release \
      "-DCMAKE_CXX_FLAGS=$warning_flags"
  fi
}

run_variant() {
  variant=$1
  shared=$2
  build_directory="$build_root/$variant"
  install_directory="$build_root/install-$variant"
  consumer_directory="$build_root/consumer-$variant"
  relocated_install_directory="$build_root/install-$variant-relocated"
  relocated_consumer_directory="$build_root/consumer-$variant-relocated"
  subdirectory_consumer_directory="$build_root/subdirectory-consumer-$variant"

  cmake -S "$repository_root" -B "$build_directory" \
    -G Ninja \
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" \
    "-DVCPKG_INSTALLED_DIR=$installed_directory" \
    -DVCPKG_TARGET_TRIPLET=x64-linux \
    -DMMXISF_ENFORCE_PRODUCTION_DEPENDENCY_BASELINE=ON \
    -DMMXISF_BUILD_TESTS=ON \
    -DMMXISF_BUILD_TOOLS=ON \
    -DMMXISF_BUILD_VIEWER=OFF \
    -DMMXISF_BUILD_DOCS=ON \
    "-DBUILD_SHARED_LIBS=$shared" \
    -DCMAKE_BUILD_TYPE=Release \
    "-DCMAKE_CXX_FLAGS=$warning_flags"
  cmake --build "$build_directory" --parallel "$parallel_jobs"
  ctest --test-dir "$build_directory" --output-on-failure
  cmake --install "$build_directory" --prefix "$install_directory"

  configure_installed_consumer "$consumer_directory" "$install_directory" \
    "$variant"
  cmake --build "$consumer_directory" --parallel "$parallel_jobs"
  ctest --test-dir "$consumer_directory" --output-on-failure

  cmake -E copy_directory "$install_directory" \
    "$relocated_install_directory"
  configure_installed_consumer "$relocated_consumer_directory" \
    "$relocated_install_directory" "$variant"
  cmake --build "$relocated_consumer_directory" \
    --parallel "$parallel_jobs"
  ctest --test-dir "$relocated_consumer_directory" --output-on-failure

  cmake -S "$repository_root/tests/subdirectory_consumer" \
    -B "$subdirectory_consumer_directory" \
    -G Ninja \
    "-DMMXISF_SOURCE_DIR=$repository_root" \
    "-DBUILD_SHARED_LIBS=$shared" \
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" \
    "-DVCPKG_INSTALLED_DIR=$installed_directory" \
    -DVCPKG_TARGET_TRIPLET=x64-linux \
    -DCMAKE_BUILD_TYPE=Release \
    "-DCMAKE_CXX_FLAGS=$warning_flags"
  cmake --build "$subdirectory_consumer_directory" \
    --parallel "$parallel_jobs"
  ctest --test-dir "$subdirectory_consumer_directory" --output-on-failure

  test -s "$build_directory/binary-dependencies.spdx.json"
  test -s "$install_directory/share/mmxisf/sbom/binary-dependencies.spdx.json"
}

export VCPKG_DISABLE_METRICS=1
run_variant static OFF
run_variant shared ON

test -s "$build_root/install-static/lib/libmmxisf.a"
test -s "$build_root/install-shared/lib/libmmxisf.so"
printf '%s\n' "mmxisf vcpkg Linux amd64 production-baseline gate: PASS"
