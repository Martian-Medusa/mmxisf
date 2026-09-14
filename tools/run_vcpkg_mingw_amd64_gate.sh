#!/bin/sh

set -eu

script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository_root=$(CDPATH= cd -- "$script_directory/.." && pwd)
vcpkg_root=${MMXISF_VCPKG_ROOT:-}
build_root=${MMXISF_VCPKG_BUILD_ROOT:-"$repository_root/build-vcpkg-mingw-amd64"}
parallel_jobs=${MMXISF_VCPKG_JOBS:-2}
warning_flags=${MMXISF_VCPKG_CXX_FLAGS:-"-Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers"}

if [ "$(uname -s)" != Linux ] || [ "$(uname -m)" != x86_64 ]; then
  printf '%s\n' "This gate requires Linux amd64" >&2
  exit 2
fi

for command_name in \
    x86_64-w64-mingw32-g++ \
    x86_64-w64-mingw32-windres \
    wine64; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    printf '%s\n' "Missing required command: $command_name" >&2
    exit 2
  fi
done

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
actual_baseline=$(git -c "safe.directory=$vcpkg_root" \
  -C "$vcpkg_root" rev-parse HEAD)
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

mingw_libstdcpp=/usr/lib/gcc/x86_64-w64-mingw32/13-posix/libstdc++-6.dll
mingw_libgcc=/usr/lib/gcc/x86_64-w64-mingw32/13-posix/libgcc_s_seh-1.dll
mingw_winpthread=/usr/x86_64-w64-mingw32/lib/libwinpthread-1.dll
for runtime_library in \
    "$mingw_libstdcpp" \
    "$mingw_libgcc" \
    "$mingw_winpthread"; do
  if [ ! -f "$runtime_library" ]; then
    printf '%s\n' "Missing MinGW runtime: $runtime_library" >&2
    exit 2
  fi
done

toolchain="$vcpkg_root/scripts/buildsystems/vcpkg.cmake"
installed_directory="$build_root/vcpkg_installed"
wine_emulator=$(command -v wine64)

copy_mingw_runtime() {
  destination=$1
  cmake -E copy_if_different "$mingw_libstdcpp" "$destination"
  cmake -E copy_if_different "$mingw_libgcc" "$destination"
  cmake -E copy_if_different "$mingw_winpthread" "$destination"
}

configure_cross_project() {
  source_directory=$1
  binary_directory=$2
  shift 2
  cmake -S "$source_directory" -B "$binary_directory" \
    -G Ninja \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
    -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" \
    -DVCPKG_TARGET_TRIPLET=x64-mingw-static \
    -DVCPKG_HOST_TRIPLET=x64-linux \
    "-DVCPKG_INSTALLED_DIR=$installed_directory" \
    "-DCMAKE_CROSSCOMPILING_EMULATOR=$wine_emulator" \
    -DCMAKE_BUILD_TYPE=Release \
    "-DCMAKE_CXX_FLAGS=$warning_flags" \
    -DCMAKE_SHARED_LINKER_FLAGS=-Wl,--no-insert-timestamp \
    "$@"
}

configure_installed_consumer() {
  consumer_directory=$1
  install_directory=$2
  linkage=$3
  if [ "$linkage" = shared ]; then
    configure_cross_project \
      "$repository_root/tests/package_consumer" \
      "$consumer_directory" \
      --no-warn-unused-cli \
      "-DCMAKE_PREFIX_PATH=$install_directory" \
      "-DMMXISF_EXPECTED_PACKAGE_PREFIX=$install_directory" \
      -DMMXISF_EXPECTED_LINKAGE=shared \
      -DCMAKE_DISABLE_FIND_PACKAGE_EXPAT=TRUE \
      -DCMAKE_DISABLE_FIND_PACKAGE_LZ4=TRUE \
      -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=TRUE \
      -DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=TRUE \
      -DCMAKE_DISABLE_FIND_PACKAGE_ZSTD=TRUE
  else
    configure_cross_project \
      "$repository_root/tests/package_consumer" \
      "$consumer_directory" \
      "-DCMAKE_PREFIX_PATH=$install_directory;$installed_directory/x64-mingw-static" \
      "-DMMXISF_EXPECTED_PACKAGE_PREFIX=$install_directory" \
      -DMMXISF_EXPECTED_LINKAGE=static
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

  configure_cross_project "$repository_root" "$build_directory" \
    -DMMXISF_ENFORCE_PRODUCTION_DEPENDENCY_BASELINE=ON \
    -DMMXISF_BUILD_TESTS=ON \
    -DMMXISF_BUILD_TOOLS=ON \
    -DMMXISF_BUILD_VIEWER=OFF \
    -DMMXISF_BUILD_DOCS=OFF \
    "-DBUILD_SHARED_LIBS=$shared"
  cmake --build "$build_directory" --parallel "$parallel_jobs"
  copy_mingw_runtime "$build_directory"
  ctest --test-dir "$build_directory" --output-on-failure --timeout 60
  cmake --install "$build_directory" --prefix "$install_directory"

  configure_installed_consumer "$consumer_directory" "$install_directory" \
    "$variant"
  cmake --build "$consumer_directory" --parallel "$parallel_jobs"
  copy_mingw_runtime "$consumer_directory"
  if [ "$variant" = shared ]; then
    cmake -E copy_if_different \
      "$install_directory/bin/libmmxisf.dll" "$consumer_directory"
  fi
  ctest --test-dir "$consumer_directory" --output-on-failure --timeout 60

  cmake -E copy_directory "$install_directory" \
    "$relocated_install_directory"
  configure_installed_consumer "$relocated_consumer_directory" \
    "$relocated_install_directory" "$variant"
  cmake --build "$relocated_consumer_directory" \
    --parallel "$parallel_jobs"
  copy_mingw_runtime "$relocated_consumer_directory"
  if [ "$variant" = shared ]; then
    cmake -E copy_if_different \
      "$relocated_install_directory/bin/libmmxisf.dll" \
      "$relocated_consumer_directory"
  fi
  ctest --test-dir "$relocated_consumer_directory" \
    --output-on-failure --timeout 60

  configure_cross_project \
    "$repository_root/tests/subdirectory_consumer" \
    "$subdirectory_consumer_directory" \
    "-DMMXISF_SOURCE_DIR=$repository_root" \
    "-DBUILD_SHARED_LIBS=$shared"
  cmake --build "$subdirectory_consumer_directory" \
    --parallel "$parallel_jobs"
  copy_mingw_runtime "$subdirectory_consumer_directory"
  if [ "$variant" = shared ]; then
    cmake -E copy_if_different \
      "$subdirectory_consumer_directory/mmxisf-source/libmmxisf.dll" \
      "$subdirectory_consumer_directory"
  fi
  ctest --test-dir "$subdirectory_consumer_directory" \
    --output-on-failure --timeout 60

  test -s "$build_directory/binary-dependencies.spdx.json"
  test -s "$install_directory/share/mmxisf/sbom/binary-dependencies.spdx.json"

  configure_cross_project "$repository_root" "$reproduction_directory" \
    -DMMXISF_ENFORCE_PRODUCTION_DEPENDENCY_BASELINE=ON \
    -DMMXISF_BUILD_TESTS=OFF \
    -DMMXISF_BUILD_TOOLS=OFF \
    -DMMXISF_BUILD_EXAMPLES=OFF \
    -DMMXISF_BUILD_VIEWER=OFF \
    -DMMXISF_BUILD_DOCS=OFF \
    -DMMXISF_INSTALL=OFF \
    "-DBUILD_SHARED_LIBS=$shared"
  cmake --build "$reproduction_directory" --target mmxisf \
    --parallel "$parallel_jobs"
  if [ "$shared" = ON ]; then
    cmake -E compare_files \
      "$build_directory/libmmxisf.dll" \
      "$reproduction_directory/libmmxisf.dll"
    cmake -E compare_files \
      "$build_directory/libmmxisf.dll.a" \
      "$reproduction_directory/libmmxisf.dll.a"
  else
    cmake -E compare_files \
      "$build_directory/libmmxisf.a" \
      "$reproduction_directory/libmmxisf.a"
  fi
}

export VCPKG_DISABLE_METRICS=1
run_variant static OFF
run_variant shared ON

test -s "$build_root/install-static/lib/libmmxisf.a"
test -s "$build_root/install-shared/bin/libmmxisf.dll"
test -s "$build_root/install-shared/lib/libmmxisf.dll.a"
printf '%s\n' "mmxisf vcpkg MinGW amd64 Wine gate: PASS"
