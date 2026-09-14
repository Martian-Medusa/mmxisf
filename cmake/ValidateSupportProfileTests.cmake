# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED MMXISF_SOURCE_DIR OR NOT DEFINED MMXISF_BINARY_DIR)
  message(FATAL_ERROR "MMXISF_SOURCE_DIR and MMXISF_BINARY_DIR are required")
endif()

set(_validator "${MMXISF_SOURCE_DIR}/cmake/ValidateSupportProfile.cmake")
set(_profile "${MMXISF_SOURCE_DIR}/docs/support-profile-0.1.0.json")
file(READ "${_profile}" _valid)
file(MAKE_DIRECTORY "${MMXISF_BINARY_DIR}/support-profile-contract")

function(mmxisf_expect_profile_rejection name content expected)
  set(_path
    "${MMXISF_BINARY_DIR}/support-profile-contract/${name}.json")
  file(WRITE "${_path}" "${content}")
  execute_process(
    COMMAND
      "${CMAKE_COMMAND}"
      "-DMMXISF_SOURCE_DIR=${MMXISF_SOURCE_DIR}"
      "-DMMXISF_SUPPORT_PROFILE_FILE=${_path}"
      -P "${_validator}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
  )
  if(_result EQUAL 0)
    message(FATAL_ERROR "Invalid support profile '${name}' was accepted")
  endif()
  set(_combined "${_stdout}\n${_stderr}")
  if(NOT _combined MATCHES "${expected}")
    message(FATAL_ERROR
      "Unexpected rejection for '${name}':\n${_combined}")
  endif()
endfunction()

set(_wrong_hash "${_valid}")
string(REPLACE
  "cfd74ea0a2d314f00e1a572442559426f2707957a0c5e417abbb5ba06c978ca9"
  "0000000000000000000000000000000000000000000000000000000000000000"
  _wrong_hash "${_wrong_hash}")
mmxisf_expect_profile_rejection(wrong-matrix-hash "${_wrong_hash}"
  "Support-profile matrix SHA-256 mismatch")

set(_viewer "${_valid}")
string(REPLACE "\"viewerIncluded\": false" "\"viewerIncluded\": true"
  _viewer "${_viewer}")
mmxisf_expect_profile_rejection(viewer-included "${_viewer}"
  "optional viewer cannot be part of library readiness")

set(_duplicate "${_valid}")
string(REPLACE "\"container.signature\","
  "\"container.signature\",\n      \"container.signature\"," _duplicate
  "${_duplicate}")
mmxisf_expect_profile_rejection(duplicate-row "${_duplicate}"
  "Duplicate support-profile row: container.signature")

set(_missing "${_valid}")
string(REPLACE "      \"container.signature\",\n" "" _missing
  "${_missing}")
mmxisf_expect_profile_rejection(missing-row "${_missing}"
  "Missing support-profile row: container.signature")

set(_frozen_without_ref "${_valid}")
string(REPLACE "\"state\": \"PREPARED\"" "\"state\": \"FROZEN\""
  _frozen_without_ref "${_frozen_without_ref}")
mmxisf_expect_profile_rejection(frozen-without-ref
  "${_frozen_without_ref}"
  "FROZEN support profile requires an immutable vX.Y.Z-rc.N ref")

set(_missing_platform "${_valid}")
string(REPLACE ",\n    \"Windows-amd64\"" "" _missing_platform
  "${_missing_platform}")
mmxisf_expect_profile_rejection(missing-platform "${_missing_platform}"
  "requires exactly three target platforms")

message(STATUS "Support-profile negative contracts: PASS")
