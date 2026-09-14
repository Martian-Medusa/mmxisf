cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED MMXISF_SOURCE_DIR OR NOT DEFINED MMXISF_BINARY_DIR)
  message(FATAL_ERROR "MMXISF_SOURCE_DIR and MMXISF_BINARY_DIR are required")
endif()

set(_validator "${MMXISF_SOURCE_DIR}/cmake/ValidatePublicApiBaseline.cmake")
set(_baseline
  "${MMXISF_SOURCE_DIR}/docs/public-api-baseline-0.1.0.json")
file(READ "${_baseline}" _valid)
set(_contract_root "${MMXISF_BINARY_DIR}/public-api-baseline-contract")
file(MAKE_DIRECTORY "${_contract_root}")

function(mmxisf_expect_api_baseline_rejection name content expected)
  set(_path "${_contract_root}/${name}.json")
  file(WRITE "${_path}" "${content}")
  execute_process(
    COMMAND
      "${CMAKE_COMMAND}"
      "-DMMXISF_SOURCE_DIR=${MMXISF_SOURCE_DIR}"
      "-DMMXISF_PUBLIC_API_BASELINE_FILE=${_path}"
      -P "${_validator}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error
  )
  if(_result EQUAL 0)
    message(FATAL_ERROR "Public API validator accepted invalid case ${name}")
  endif()
  string(CONCAT _diagnostic "${_output}" "${_error}")
  if(NOT _diagnostic MATCHES "${expected}")
    message(FATAL_ERROR
      "Public API case ${name} missed expected diagnostic ${expected}: ${_diagnostic}")
  endif()
endfunction()

string(REPLACE
  "b42dafb3e99a9e9e55e777a3389cad429092f6f133adc7592644b423fd8f56b6"
  "0000000000000000000000000000000000000000000000000000000000000000"
  _wrong_hash "${_valid}")
mmxisf_expect_api_baseline_rejection(wrong-hash "${_wrong_hash}"
  "header SHA-256 mismatch")

string(REPLACE
  "include/mmxisf/byte_source.hpp"
  "include/mmxisf/byte_sink.hpp"
  _duplicate_header "${_valid}")
mmxisf_expect_api_baseline_rejection(duplicate-header "${_duplicate_header}"
  "Duplicate public API header path")

string(REPLACE
  "    },\n    {\n      \"path\": \"include/mmxisf/writer.hpp\",\n      \"sha256\": \"0d911b8a0738b6056c9d9deb73ddf6bb7a2a1d8d6d0d25f97bc8b76dfd31aa1b\"\n    }\n"
  "    }\n"
  _missing_header "${_valid}")
mmxisf_expect_api_baseline_rejection(missing-header "${_missing_header}"
  "baseline has .* headers; source has")

string(REPLACE
  "\"abiPolicy\": \"NO_ABI_PROMISE_BEFORE_1.0\""
  "\"abiPolicy\": \"STABLE_ABI\""
  _wrong_policy "${_valid}")
mmxisf_expect_api_baseline_rejection(wrong-policy "${_wrong_policy}"
  "Unexpected public API ABI policy")

message(STATUS "Public API baseline negative contracts: PASS")
