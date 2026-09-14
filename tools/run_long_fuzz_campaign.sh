#!/bin/sh

set -eu

script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository_root=$(CDPATH= cd -- "$script_directory/.." && pwd)
campaign_root=${MMXISF_FUZZ_CAMPAIGN_ROOT:-"$repository_root/build-fuzz-long"}
duration_seconds=${MMXISF_FUZZ_DURATION_SECONDS:-900}
maximum_input_bytes=${MMXISF_FUZZ_MAX_INPUT_BYTES:-1048576}
input_timeout_seconds=${MMXISF_FUZZ_INPUT_TIMEOUT_SECONDS:-10}
rss_limit_mib=${MMXISF_FUZZ_RSS_LIMIT_MIB:-4096}
parallel_jobs=${MMXISF_FUZZ_BUILD_JOBS:-2}
fuzz_cc=${MMXISF_FUZZ_CC:-${CC:-clang}}
fuzz_cxx=${MMXISF_FUZZ_CXX:-${CXX:-clang++}}

validate_positive_integer()
{
  value_name=$1
  value=$2
  case "$value" in
    ""|*[!0-9]*)
      printf '%s\n' "$value_name must be a positive integer" >&2
      exit 2
      ;;
  esac
  if [ "$value" -lt 1 ]; then
    printf '%s\n' "$value_name must be a positive integer" >&2
    exit 2
  fi
}

validate_positive_integer MMXISF_FUZZ_DURATION_SECONDS "$duration_seconds"
validate_positive_integer MMXISF_FUZZ_MAX_INPUT_BYTES "$maximum_input_bytes"
validate_positive_integer MMXISF_FUZZ_INPUT_TIMEOUT_SECONDS \
  "$input_timeout_seconds"
validate_positive_integer MMXISF_FUZZ_RSS_LIMIT_MIB "$rss_limit_mib"
validate_positive_integer MMXISF_FUZZ_BUILD_JOBS "$parallel_jobs"

if [ -e "$campaign_root" ]; then
  printf '%s\n' \
    "Refusing to overwrite fuzz campaign directory: $campaign_root" >&2
  exit 2
fi

env CC="$fuzz_cc" CXX="$fuzz_cxx" \
  cmake -S "$repository_root" -B "$campaign_root" \
  -DMMXISF_BUILD_TESTS=OFF \
  -DMMXISF_BUILD_TOOLS=OFF \
  -DMMXISF_BUILD_VIEWER=OFF \
  -DMMXISF_BUILD_FUZZER=ON \
  -DMMXISF_BUILD_FUZZ_SMOKE=OFF \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build "$campaign_root" --parallel "$parallel_jobs"

mkdir -p "$campaign_root/corpus" "$campaign_root/artifacts"
for seed in "$repository_root"/tests/fuzz_seed*.xisf.b64; do
  base64 --decode "$seed" > \
    "$campaign_root/corpus/$(basename "$seed" .b64)"
done
for seed in "$repository_root"/tests/interop/*.xisf.b64; do
  base64 --decode "$seed" > \
    "$campaign_root/corpus/interop-$(basename "$seed" .b64)"
done

set +e
"$campaign_root/mmxisf_fuzz_header" \
  "-max_total_time=$duration_seconds" \
  "-max_len=$maximum_input_bytes" \
  "-timeout=$input_timeout_seconds" \
  "-rss_limit_mb=$rss_limit_mib" \
  -print_final_stats=1 \
  "-dict=$repository_root/tests/fuzz_header.dict" \
  "-artifact_prefix=$campaign_root/artifacts/" \
  "$campaign_root/corpus" >"$campaign_root/campaign.log" 2>&1
campaign_status=$?
set -e

tail -n 80 "$campaign_root/campaign.log"
if [ "$campaign_status" -ne 0 ]; then
  printf '%s\n' "mmxisf long fuzz campaign: FAIL ($campaign_status)" >&2
  exit "$campaign_status"
fi

printf '%s\n' "mmxisf long fuzz campaign: PASS"
