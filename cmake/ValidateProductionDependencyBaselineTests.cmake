# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED MMXISF_SOURCE_DIR)
  message(FATAL_ERROR "MMXISF_SOURCE_DIR is required")
endif()

set(_validator
  "${MMXISF_SOURCE_DIR}/cmake/EnforceProductionDependencyBaseline.cmake")
if(NOT EXISTS "${_validator}")
  message(FATAL_ERROR "Missing production dependency validator: ${_validator}")
endif()

function(mmxisf_run_baseline_case name expect_success
    expat zlib lz4 zstd openssl expected_text)
  execute_process(
    COMMAND
      "${CMAKE_COMMAND}"
      "-DMMXISF_EXPAT_VERSION=${expat}"
      "-DMMXISF_ZLIB_VERSION=${zlib}"
      "-DMMXISF_LZ4_VERSION=${lz4}"
      "-DMMXISF_ZSTD_VERSION=${zstd}"
      "-DMMXISF_OPENSSL_VERSION=${openssl}"
      -P "${_validator}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
  )
  set(_combined "${_stdout}\n${_stderr}")
  if(expect_success)
    if(NOT _result EQUAL 0)
      message(FATAL_ERROR
        "Baseline case ${name} unexpectedly failed:\n${_combined}"
      )
    endif()
  else()
    if(_result EQUAL 0)
      message(FATAL_ERROR "Baseline case ${name} unexpectedly passed")
    endif()
    if(NOT _combined MATCHES "${expected_text}")
      message(FATAL_ERROR
        "Baseline case ${name} did not report ${expected_text}:\n${_combined}"
      )
    endif()
  endif()
endfunction()

mmxisf_run_baseline_case(
  exact-floor TRUE 2.8.2 1.3.2 1.10.0 1.5.7 3.5.8 ""
)
mmxisf_run_baseline_case(
  current-development FALSE 2.5.0 1.3 1.9.4 1.5.5 3.2.0
  "Expat 2.5.0 is below"
)
mmxisf_run_baseline_case(
  expat-low FALSE 2.8.1 1.3.2 1.10.0 1.5.7 3.5.8
  "Expat 2.8.1 is below"
)
mmxisf_run_baseline_case(
  zlib-low FALSE 2.8.2 1.3.1 1.10.0 1.5.7 3.5.8
  "zlib 1.3.1 is below"
)
mmxisf_run_baseline_case(
  lz4-low FALSE 2.8.2 1.3.2 1.9.4 1.5.7 3.5.8
  "LZ4 1.9.4 is below"
)
mmxisf_run_baseline_case(
  zstd-low FALSE 2.8.2 1.3.2 1.10.0 1.5.6 3.5.8
  "Zstandard 1.5.6 is below"
)
mmxisf_run_baseline_case(
  openssl-low FALSE 2.8.2 1.3.2 1.10.0 1.5.7 3.5.7
  "OpenSSL 3.5.7 is below"
)
