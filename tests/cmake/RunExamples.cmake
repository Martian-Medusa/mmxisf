# SPDX-License-Identifier: Apache-2.0

foreach(_required IN ITEMS
    MMXISF_READ_EXAMPLE
    MMXISF_WRITE_EXAMPLE
    MMXISF_EXAMPLE_OUTPUT)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "${_required} is required")
  endif()
endforeach()

file(REMOVE "${MMXISF_EXAMPLE_OUTPUT}")
execute_process(
  COMMAND "${MMXISF_WRITE_EXAMPLE}" "${MMXISF_EXAMPLE_OUTPUT}"
  RESULT_VARIABLE _write_result
  OUTPUT_VARIABLE _write_output
  ERROR_VARIABLE _write_error
)
if(NOT _write_result EQUAL 0 OR
   NOT EXISTS "${MMXISF_EXAMPLE_OUTPUT}" OR
   NOT _write_output MATCHES "^wrote [0-9]+ bytes")
  file(REMOVE "${MMXISF_EXAMPLE_OUTPUT}")
  message(FATAL_ERROR
    "Writer example failed (${_write_result}): ${_write_error}${_write_output}")
endif()

execute_process(
  COMMAND "${MMXISF_READ_EXAMPLE}" "${MMXISF_EXAMPLE_OUTPUT}"
  RESULT_VARIABLE _read_result
  OUTPUT_VARIABLE _read_output
  ERROR_VARIABLE _read_error
)
file(REMOVE "${MMXISF_EXAMPLE_OUTPUT}")
if(NOT _read_result EQUAL 0 OR
   NOT _read_output MATCHES "images: 1" OR
   NOT _read_output MATCHES "first image: 2x2 Gray UInt16" OR
   NOT _read_output MATCHES "decoded bytes: 8")
  message(FATAL_ERROR
    "Reader example failed (${_read_result}): ${_read_error}${_read_output}")
endif()
