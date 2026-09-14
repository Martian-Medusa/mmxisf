# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED MMXISF_VALIDATOR OR NOT EXISTS "${MMXISF_VALIDATOR}" OR
   NOT DEFINED MMXISF_CONTRACT_DIRECTORY)
  message(FATAL_ERROR
    "MMXISF_VALIDATOR and MMXISF_CONTRACT_DIRECTORY are required")
endif()

file(MAKE_DIRECTORY "${MMXISF_CONTRACT_DIRECTORY}")
set(_dummy_library "${MMXISF_CONTRACT_DIRECTORY}/mmxisf.dll")
set(_valid_output "${MMXISF_CONTRACT_DIRECTORY}/valid-exports.txt")
set(_unexpected_output
  "${MMXISF_CONTRACT_DIRECTORY}/unexpected-exports.txt")
set(_empty_output "${MMXISF_CONTRACT_DIRECTORY}/empty-exports.txt")
file(WRITE "${_dummy_library}" "contract fixture\n")
file(WRITE "${_valid_output}"
  "Dump of file mmxisf.dll\n\n"
  "  ordinal hint RVA      name\n\n"
  "        1    0 00011000 ?version@mmxisf@@YA?AUVersion@1@XZ\n"
  "        2    1 00012000 ?open_file@Reader@mmxisf@@SA?AV12@XZ\n\n"
  "  Summary\n")
file(WRITE "${_unexpected_output}"
  "  ordinal hint RVA      name\n\n"
  "        1    0 00011000 ?version@mmxisf@@YA?AUVersion@1@XZ\n"
  "        2    1 00012000 unexpected_api\n\n"
  "  Summary\n")
file(WRITE "${_empty_output}"
  "Dump of file mmxisf.dll\n\n"
  "  ordinal hint RVA      name\n\n"
  "  Summary\n")

function(mmxisf_run_fixture fixture expected_success expected_text)
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      "-DMMXISF_SHARED_LIBRARY=${_dummy_library}"
      -DMMXISF_PLATFORM=Windows
      -DMMXISF_COMPILER_ID=MSVC
      "-DMMXISF_EXPORT_TOOL_OUTPUT_FILE=${fixture}"
      -P "${MMXISF_VALIDATOR}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error
  )
  set(_combined "${_output}${_error}")
  if(expected_success AND NOT _result EQUAL 0)
    message(FATAL_ERROR
      "Valid MSVC export fixture failed: ${_combined}")
  elseif(NOT expected_success AND _result EQUAL 0)
    message(FATAL_ERROR "Invalid MSVC export fixture unexpectedly passed")
  endif()
  if(NOT _combined MATCHES "${expected_text}")
    message(FATAL_ERROR
      "MSVC export fixture omitted expected diagnostic '${expected_text}': "
      "${_combined}")
  endif()
endfunction()

mmxisf_run_fixture("${_valid_output}" true
  "Validated shared export surface: 2 mmxisf symbols, 2 total")
mmxisf_run_fixture("${_unexpected_output}" false
  "exports 1 non-mmxisf symbols; first: unexpected_api")
mmxisf_run_fixture("${_empty_output}" false
  "Shared library exports no mmxisf API symbols")

message(STATUS "Validated MSVC shared-export parser contracts")
