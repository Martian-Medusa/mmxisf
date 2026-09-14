#!/bin/sh

set -eu

script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository_root=$(CDPATH= cd -- "$script_directory/.." && pwd)
gate_root=${MMXISF_LOCAL_GATE_ROOT:-"$repository_root/build-local-gates"}
parallel_jobs=${MMXISF_LOCAL_JOBS:-2}
warning_flags=${MMXISF_LOCAL_CXX_FLAGS:-"-Wall -Wextra -Wpedantic -Werror"}
thread_sanitizer_enabled=${MMXISF_LOCAL_TSAN:-ON}
viewer_enabled=${MMXISF_LOCAL_VIEWER:-OFF}
sanitizer_cc=${MMXISF_LOCAL_SANITIZER_CC:-${CC:-cc}}
sanitizer_cxx=${MMXISF_LOCAL_SANITIZER_CXX:-${CXX:-c++}}

case "$viewer_enabled" in
  ON|OFF)
    ;;
  *)
    printf '%s\n' "MMXISF_LOCAL_VIEWER must be ON or OFF" >&2
    exit 2
    ;;
esac
if [ "$viewer_enabled" = ON ] && [ "$(uname -s)" != Darwin ]; then
  printf '%s\n' "MMXISF_LOCAL_VIEWER=ON requires macOS" >&2
  exit 2
fi

configure_build_test()
{
  build_directory=$1
  linkage=$2
  viewer=$3
  cmake -S "$repository_root" -B "$build_directory" \
    -DMMXISF_BUILD_TESTS=ON \
    -DMMXISF_BUILD_VIEWER="$viewer" \
    -DBUILD_SHARED_LIBS="$linkage" \
    -DCMAKE_BUILD_TYPE=Release \
    "-DCMAKE_CXX_FLAGS=$warning_flags" \
    "-DMMXISF_VIEWER_OUTPUT_DIR=$gate_root/artifacts"
  cmake --build "$build_directory" --parallel "$parallel_jobs"
  ctest --test-dir "$build_directory" -C Release --output-on-failure
}

configure_installed_consumer()
{
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
      "-DCMAKE_PREFIX_PATH=$install_directory" \
      "-DMMXISF_EXPECTED_PACKAGE_PREFIX=$install_directory" \
      -DMMXISF_EXPECTED_LINKAGE=static \
      -DCMAKE_BUILD_TYPE=Release \
      "-DCMAKE_CXX_FLAGS=$warning_flags"
  fi
}

verify_installed_package()
{
  build_directory=$1
  install_directory=$2
  consumer_directory=$3
  linkage=$4
  relocated_install_directory="$install_directory-relocated"
  relocated_consumer_directory="$consumer_directory-relocated"
  cmake --install "$build_directory" --config Release \
    --prefix "$install_directory"
  configure_installed_consumer "$consumer_directory" "$install_directory" \
    "$linkage"
  cmake --build "$consumer_directory" --parallel "$parallel_jobs"
  ctest --test-dir "$consumer_directory" -C Release --output-on-failure

  cmake -E remove_directory "$relocated_install_directory"
  cmake -E copy_directory "$install_directory" \
    "$relocated_install_directory"
  configure_installed_consumer "$relocated_consumer_directory" \
    "$relocated_install_directory" "$linkage"
  cmake --build "$relocated_consumer_directory" --parallel "$parallel_jobs"
  ctest --test-dir "$relocated_consumer_directory" -C Release \
    --output-on-failure
}

verify_subdirectory_consumer()
{
  consumer_directory=$1
  linkage=$2
  cmake -S "$repository_root/tests/subdirectory_consumer" \
    -B "$consumer_directory" \
    "-DMMXISF_SOURCE_DIR=$repository_root" \
    "-DBUILD_SHARED_LIBS=$linkage" \
    -DCMAKE_BUILD_TYPE=Release \
    "-DCMAKE_CXX_FLAGS=$warning_flags"
  cmake --build "$consumer_directory" --parallel "$parallel_jobs"
  ctest --test-dir "$consumer_directory" -C Release --output-on-failure
}

mkdir -p "$gate_root"

configure_build_test "$gate_root/static" OFF OFF
verify_installed_package "$gate_root/static" "$gate_root/install-static" \
  "$gate_root/consumer-static" static

configure_build_test "$gate_root/shared" ON OFF
verify_installed_package "$gate_root/shared" "$gate_root/install-shared" \
  "$gate_root/consumer-shared" shared

verify_subdirectory_consumer "$gate_root/subdirectory-consumer-static" OFF
verify_subdirectory_consumer "$gate_root/subdirectory-consumer-shared" ON

env CC="$sanitizer_cc" CXX="$sanitizer_cxx" \
  cmake -S "$repository_root" -B "$gate_root/sanitizers" \
  -DMMXISF_BUILD_TESTS=ON \
  -DMMXISF_BUILD_TOOLS=OFF \
  -DMMXISF_BUILD_FUZZ_SMOKE=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  "-DCMAKE_CXX_FLAGS=$warning_flags"
cmake --build "$gate_root/sanitizers" --parallel "$parallel_jobs"
ctest --test-dir "$gate_root/sanitizers" --output-on-failure
"$gate_root/sanitizers/mmxisf_fuzz_smoke"

if [ "$thread_sanitizer_enabled" = ON ]; then
  env CC="$sanitizer_cc" CXX="$sanitizer_cxx" \
    cmake -S "$repository_root" -B "$gate_root/thread-sanitizer" \
    -DMMXISF_BUILD_TESTS=ON \
    -DMMXISF_BUILD_TOOLS=OFF \
    -DMMXISF_BUILD_VIEWER=OFF \
    -DCMAKE_BUILD_TYPE=Debug \
    "-DCMAKE_CXX_FLAGS=$warning_flags -fsanitize=thread -fno-omit-frame-pointer" \
    "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread" \
    "-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=thread"
  cmake --build "$gate_root/thread-sanitizer" --parallel "$parallel_jobs"
  ctest --test-dir "$gate_root/thread-sanitizer" --output-on-failure
fi

cmake -S "$repository_root" -B "$gate_root/docs" \
  -DMMXISF_BUILD_DOCS=ON \
  -DMMXISF_BUILD_TESTS=OFF \
  -DMMXISF_BUILD_TOOLS=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$gate_root/docs" --target mmxisf_docs \
  --parallel "$parallel_jobs"
test -s "$gate_root/docs/api/html/index.html"

if [ "$viewer_enabled" = ON ]; then
  configure_build_test "$gate_root/viewer" OFF ON
  viewer_bundle="$gate_root/artifacts/mmXISF Viewer PoC.app"
  codesign --verify --deep --strict --verbose=2 "$viewer_bundle"
fi

printf '%s\n' "mmxisf local quality gates: PASS"
