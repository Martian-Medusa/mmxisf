#!/bin/sh

set -eu

script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository_root=$(CDPATH= cd -- "$script_directory/.." && pwd)
vcpkg_root=${MMXISF_VCPKG_ROOT:-}
build_root=${MMXISF_VCPKG_BUILD_ROOT:-"$repository_root/build-vcpkg-macos-arm64"}
parallel_jobs=${MMXISF_VCPKG_JOBS:-2}
warning_flags=${MMXISF_VCPKG_CXX_FLAGS:-"-Wall -Wextra -Wpedantic -Werror"}
viewer_enabled=${MMXISF_VCPKG_VIEWER:-OFF}

if [ "$(uname -s)" != Darwin ] || [ "$(uname -m)" != arm64 ]; then
  printf '%s\n' "This gate requires macOS arm64" >&2
  exit 2
fi
case "$viewer_enabled" in
  ON|OFF)
    ;;
  *)
    printf '%s\n' "MMXISF_VCPKG_VIEWER must be ON or OFF" >&2
    exit 2
    ;;
esac

if [ -z "$vcpkg_root" ] ||
   [ ! -x "$vcpkg_root/vcpkg" ] ||
   [ ! -f "$vcpkg_root/scripts/buildsystems/vcpkg.cmake" ]; then
  printf '%s\n' \
    "Set MMXISF_VCPKG_ROOT to a macOS-bootstrapped official vcpkg checkout" >&2
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

# Apple's archiver otherwise records member mtimes in static libraries.
export ZERO_AR_DATE=1
export VCPKG_DISABLE_METRICS=1

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
      "-DCMAKE_PREFIX_PATH=$install_directory;$installed_directory/arm64-osx" \
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
  reproduction_directory="$build_root/reproduction-$variant"

  cmake -S "$repository_root" -B "$build_directory" \
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" \
    "-DVCPKG_INSTALLED_DIR=$installed_directory" \
    -DVCPKG_TARGET_TRIPLET=arm64-osx \
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
    "-DMMXISF_SOURCE_DIR=$repository_root" \
    "-DBUILD_SHARED_LIBS=$shared" \
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" \
    "-DVCPKG_INSTALLED_DIR=$installed_directory" \
    -DVCPKG_TARGET_TRIPLET=arm64-osx \
    -DCMAKE_BUILD_TYPE=Release \
    "-DCMAKE_CXX_FLAGS=$warning_flags"
  cmake --build "$subdirectory_consumer_directory" \
    --parallel "$parallel_jobs"
  ctest --test-dir "$subdirectory_consumer_directory" --output-on-failure

  test -s "$build_directory/binary-dependencies.spdx.json"
  test -s "$install_directory/share/mmxisf/sbom/binary-dependencies.spdx.json"

  cmake -S "$repository_root" -B "$reproduction_directory" \
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" \
    "-DVCPKG_INSTALLED_DIR=$installed_directory" \
    -DVCPKG_TARGET_TRIPLET=arm64-osx \
    -DMMXISF_ENFORCE_PRODUCTION_DEPENDENCY_BASELINE=ON \
    -DMMXISF_BUILD_TESTS=OFF \
    -DMMXISF_BUILD_TOOLS=OFF \
    -DMMXISF_BUILD_EXAMPLES=OFF \
    -DMMXISF_BUILD_VIEWER=OFF \
    -DMMXISF_BUILD_DOCS=OFF \
    -DMMXISF_INSTALL=OFF \
    "-DBUILD_SHARED_LIBS=$shared" \
    -DCMAKE_BUILD_TYPE=Release \
    "-DCMAKE_CXX_FLAGS=$warning_flags"
  cmake --build "$reproduction_directory" --target mmxisf \
    --parallel "$parallel_jobs"
  if [ "$shared" = ON ]; then
    primary_library="$build_directory/libmmxisf.0.1.0.dylib"
    reproduced_library="$reproduction_directory/libmmxisf.0.1.0.dylib"
  else
    primary_library="$build_directory/libmmxisf.a"
    reproduced_library="$reproduction_directory/libmmxisf.a"
  fi
  cmake -E compare_files "$primary_library" "$reproduced_library"
}

run_viewer() {
  viewer_directory="$build_root/viewer"
  viewer_output="$build_root/artifacts"
  cmake -S "$repository_root" -B "$viewer_directory" \
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" \
    "-DVCPKG_INSTALLED_DIR=$installed_directory" \
    -DVCPKG_TARGET_TRIPLET=arm64-osx \
    -DMMXISF_ENFORCE_PRODUCTION_DEPENDENCY_BASELINE=ON \
    -DMMXISF_BUILD_TESTS=ON \
    -DMMXISF_BUILD_TOOLS=OFF \
    -DMMXISF_BUILD_VIEWER=ON \
    -DMMXISF_BUILD_DOCS=OFF \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    "-DCMAKE_CXX_FLAGS=$warning_flags" \
    "-DMMXISF_VIEWER_OUTPUT_DIR=$viewer_output"
  cmake --build "$viewer_directory" --parallel "$parallel_jobs"
  ctest --test-dir "$viewer_directory" --output-on-failure

  viewer_bundle="$viewer_output/mmXISF Viewer PoC.app"
  viewer_executable="$viewer_bundle/Contents/MacOS/mmXISF Viewer PoC"
  codesign --verify --deep --strict --verbose=2 "$viewer_bundle"
  if ! file "$viewer_executable" | awk \
    '/Mach-O 64-bit executable arm64/ {ok=1} END {exit !ok}'; then
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
}

run_variant static OFF
run_variant shared ON

test -s "$build_root/install-static/lib/libmmxisf.a"
test -s "$build_root/install-shared/lib/libmmxisf.dylib"

if [ "$viewer_enabled" = ON ]; then
  run_viewer
fi

printf '%s\n' "mmxisf vcpkg macOS arm64 production-baseline gate: PASS"
