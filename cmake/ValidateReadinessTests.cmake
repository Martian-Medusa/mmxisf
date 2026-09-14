cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED MMXISF_SOURCE_DIR OR NOT DEFINED MMXISF_BINARY_DIR)
  message(FATAL_ERROR "MMXISF_SOURCE_DIR and MMXISF_BINARY_DIR are required")
endif()

set(_validator "${MMXISF_SOURCE_DIR}/cmake/ValidateReadiness.cmake")
set(_ledger "${MMXISF_SOURCE_DIR}/docs/production-readiness.json")
file(READ "${_ledger}" _valid_json)
file(MAKE_DIRECTORY "${MMXISF_BINARY_DIR}/readiness-contract")

function(mmxisf_expect_readiness_rejection name content expected)
  set(_path "${MMXISF_BINARY_DIR}/readiness-contract/${name}.json")
  file(WRITE "${_path}" "${content}")
  execute_process(
    COMMAND
      "${CMAKE_COMMAND}"
      "-DMMXISF_SOURCE_DIR=${MMXISF_SOURCE_DIR}"
      "-DMMXISF_READINESS_FILE=${_path}"
      -P "${_validator}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error
  )
  if(_result EQUAL 0)
    message(FATAL_ERROR "Readiness validator accepted invalid case ${name}")
  endif()
  string(CONCAT _diagnostic "${_output}" "${_error}")
  if(NOT _diagnostic MATCHES "${expected}")
    message(FATAL_ERROR
      "Readiness case ${name} missed expected diagnostic ${expected}: ${_diagnostic}")
  endif()
endfunction()

string(REPLACE
  "\"standaloneBeta\": \"NOT_READY\""
  "\"standaloneBeta\": \"READY\""
  _false_ready "${_valid_json}")
mmxisf_expect_readiness_rejection(false-ready "${_false_ready}"
  "conflicts with derived NOT_READY")

string(REPLACE
  "\"state\": \"UNFROZEN\",\n    \"commit\": \"\""
  "\"state\": \"FROZEN\",\n    \"commit\": \"short\""
  _invalid_commit "${_valid_json}")
mmxisf_expect_readiness_rejection(invalid-frozen-commit "${_invalid_commit}"
  "requires a lowercase 40-hex commit")

string(REPLACE
  "\"status\": \"LIMITED\""
  "\"status\": \"CLAIMED\""
  _invalid_status "${_valid_json}")
mmxisf_expect_readiness_rejection(invalid-status "${_invalid_status}"
  "Invalid status for readiness gate")

message(STATUS "Production readiness negative contracts: PASS")
