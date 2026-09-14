#!/bin/sh

set -eu

script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository_root=$(CDPATH= cd -- "$script_directory/.." && pwd)
gate_root=${MMXISF_LOCAL_GATE_ROOT:-"$repository_root/build-local-gates"}
parallel_jobs=${MMXISF_LOCAL_JOBS:-2}
warning_flags=${MMXISF_LOCAL_CXX_FLAGS:-"-Wall -Wextra -Wpedantic -Werror"}

case $(uname -s) in
  Darwin)
    viewer_enabled=ON
    ;;
  *)
    viewer_enabled=OFF
    ;;
esac

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

verify_installed_package()
{
  build_directory=$1
  install_directory=$2
  consumer_directory=$3
  cmake --install "$build_directory" --config Release \
    --prefix "$install_directory"
  cmake -S "$repository_root/tests/package_consumer" \
    -B "$consumer_directory" \
    "-DCMAKE_PREFIX_PATH=$install_directory" \
    -DCMAKE_BUILD_TYPE=Release \
    "-DCMAKE_CXX_FLAGS=$warning_flags"
  cmake --build "$consumer_directory" --parallel "$parallel_jobs"
  ctest --test-dir "$consumer_directory" -C Release --output-on-failure
}

mkdir -p "$gate_root"

configure_build_test "$gate_root/static" OFF "$viewer_enabled"
verify_installed_package "$gate_root/static" "$gate_root/install-static" \
  "$gate_root/consumer-static"

configure_build_test "$gate_root/shared" ON OFF
verify_installed_package "$gate_root/shared" "$gate_root/install-shared" \
  "$gate_root/consumer-shared"

cmake -S "$repository_root" -B "$gate_root/sanitizers" \
  -DMMXISF_BUILD_TESTS=ON \
  -DMMXISF_BUILD_TOOLS=OFF \
  -DMMXISF_BUILD_FUZZ_SMOKE=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  "-DCMAKE_CXX_FLAGS=$warning_flags"
cmake --build "$gate_root/sanitizers" --parallel "$parallel_jobs"
ctest --test-dir "$gate_root/sanitizers" --output-on-failure
"$gate_root/sanitizers/mmxisf_fuzz_smoke"

cmake -S "$repository_root" -B "$gate_root/docs" \
  -DMMXISF_BUILD_DOCS=ON \
  -DMMXISF_BUILD_TESTS=OFF \
  -DMMXISF_BUILD_TOOLS=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$gate_root/docs" --target mmxisf_docs \
  --parallel "$parallel_jobs"
test -s "$gate_root/docs/api/html/index.html"

if [ "$viewer_enabled" = ON ]; then
  viewer_bundle="$gate_root/artifacts/mmXISF Viewer PoC.app"
  codesign --verify --deep --strict --verbose=2 "$viewer_bundle"
fi

printf '%s\n' "mmxisf local quality gates: PASS"
