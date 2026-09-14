# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED MMXISF_SHARED_LIBRARY OR
   NOT EXISTS "${MMXISF_SHARED_LIBRARY}")
  message(FATAL_ERROR "MMXISF_SHARED_LIBRARY is required")
endif()
if(NOT DEFINED MMXISF_NM OR NOT EXISTS "${MMXISF_NM}")
  message(FATAL_ERROR "MMXISF_NM is required")
endif()

if(MMXISF_PLATFORM STREQUAL "Darwin")
  set(_nm_arguments -gU "${MMXISF_SHARED_LIBRARY}")
else()
  set(_nm_arguments -D --defined-only "${MMXISF_SHARED_LIBRARY}")
endif()

execute_process(
  COMMAND "${MMXISF_NM}" ${_nm_arguments}
  RESULT_VARIABLE _nm_result
  OUTPUT_VARIABLE _nm_output
  ERROR_VARIABLE _nm_error
)
if(NOT _nm_result EQUAL 0)
  message(FATAL_ERROR "Could not inspect shared-library exports: ${_nm_error}")
endif()

string(REPLACE "\r\n" "\n" _nm_output "${_nm_output}")
string(REPLACE "\n" ";" _nm_lines "${_nm_output}")
set(_export_count 0)
set(_mmxisf_export_count 0)
set(_unexpected)
foreach(_line IN LISTS _nm_lines)
  string(STRIP "${_line}" _line)
  if(_line STREQUAL "")
    continue()
  endif()
  math(EXPR _export_count "${_export_count} + 1")
  if(_line MATCHES "mmxisf")
    math(EXPR _mmxisf_export_count "${_mmxisf_export_count} + 1")
  elseif(NOT _line MATCHES
         " (A|B|D|R|S|T|V|W) (_init|_fini|__bss_start|_edata|_end)(@.*)?$")
    list(APPEND _unexpected "${_line}")
  endif()
endforeach()

if(_mmxisf_export_count LESS 1)
  message(FATAL_ERROR "Shared library exports no mmxisf API symbols")
endif()
if(_unexpected)
  list(LENGTH _unexpected _unexpected_count)
  list(GET _unexpected 0 _first_unexpected)
  message(FATAL_ERROR
    "Shared library exports ${_unexpected_count} non-mmxisf symbols; first: ${_first_unexpected}")
endif()

message(STATUS
  "Validated shared export surface: ${_mmxisf_export_count} mmxisf symbols, ${_export_count} total")
