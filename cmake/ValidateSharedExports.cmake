# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED MMXISF_SHARED_LIBRARY OR
   NOT EXISTS "${MMXISF_SHARED_LIBRARY}")
  message(FATAL_ERROR "MMXISF_SHARED_LIBRARY is required")
endif()
set(_export_count 0)
set(_mmxisf_export_count 0)
set(_unexpected)

if(MMXISF_PLATFORM STREQUAL "Windows")
  if(MMXISF_COMPILER_ID STREQUAL "MSVC")
    if(DEFINED MMXISF_EXPORT_TOOL_OUTPUT_FILE)
      if(NOT EXISTS "${MMXISF_EXPORT_TOOL_OUTPUT_FILE}")
        message(FATAL_ERROR "MSVC export-tool output fixture is missing")
      endif()
      file(READ "${MMXISF_EXPORT_TOOL_OUTPUT_FILE}" _link_output)
    else()
      if(NOT DEFINED MMXISF_LINKER OR NOT EXISTS "${MMXISF_LINKER}")
        message(FATAL_ERROR "CMAKE_LINKER is required for MSVC export inspection")
      endif()
      execute_process(
        COMMAND "${MMXISF_LINKER}" /dump /exports "${MMXISF_SHARED_LIBRARY}"
        RESULT_VARIABLE _link_result
        OUTPUT_VARIABLE _link_output
        ERROR_VARIABLE _link_error
      )
      if(NOT _link_result EQUAL 0)
        message(FATAL_ERROR "Could not inspect MSVC DLL exports: ${_link_error}")
      endif()
    endif()
    string(REPLACE "\r\n" "\n" _link_output "${_link_output}")
    string(REPLACE "\n" ";" _export_lines "${_link_output}")
    set(_in_export_table false)
    foreach(_line IN LISTS _export_lines)
      if(_line MATCHES
         "^[ \t]*ordinal[ \t]+hint[ \t]+RVA[ \t]+name[ \t]*$")
        set(_in_export_table true)
      elseif(_in_export_table AND
             _line MATCHES
             "^[ \t]*[0-9]+[ \t]+[0-9A-Fa-f]+[ \t]+[0-9A-Fa-f]+[ \t]+([^ \t]+)")
        set(_name "${CMAKE_MATCH_1}")
        math(EXPR _export_count "${_export_count} + 1")
        if(_name MATCHES "mmxisf")
          math(EXPR _mmxisf_export_count "${_mmxisf_export_count} + 1")
        else()
          list(APPEND _unexpected "${_name}")
        endif()
      elseif(_in_export_table AND _export_count GREATER 0 AND
             NOT _line MATCHES "^[ \t]*$")
        break()
      endif()
    endforeach()
  else()
    if(NOT DEFINED MMXISF_OBJDUMP OR NOT EXISTS "${MMXISF_OBJDUMP}")
      message(FATAL_ERROR "MMXISF_OBJDUMP is required on MinGW")
    endif()
    execute_process(
      COMMAND "${MMXISF_OBJDUMP}" -p "${MMXISF_SHARED_LIBRARY}"
      RESULT_VARIABLE _objdump_result
      OUTPUT_VARIABLE _objdump_output
      ERROR_VARIABLE _objdump_error
    )
    if(NOT _objdump_result EQUAL 0)
      message(FATAL_ERROR "Could not inspect MinGW DLL exports: ${_objdump_error}")
    endif()
    string(REPLACE "\r\n" "\n" _objdump_output "${_objdump_output}")
    string(FIND "${_objdump_output}" "[Ordinal/Name Pointer] Table"
      _name_table_offset)
    if(_name_table_offset LESS 0)
      message(FATAL_ERROR "PE image has no named export table")
    endif()
    string(SUBSTRING "${_objdump_output}" ${_name_table_offset} -1
      _name_table)
    string(REPLACE "\n" ";" _export_lines "${_name_table}")
    foreach(_line IN LISTS _export_lines)
      if(_line MATCHES "^[ \t]*\\[[ \t]*[0-9]+\\][ \t]+(.+)$")
        set(_name "${CMAKE_MATCH_1}")
        math(EXPR _export_count "${_export_count} + 1")
        if(_name MATCHES "mmxisf")
          math(EXPR _mmxisf_export_count "${_mmxisf_export_count} + 1")
        else()
          list(APPEND _unexpected "${_name}")
        endif()
      elseif(_export_count GREATER 0)
        break()
      endif()
    endforeach()
  endif()
else()
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
    message(FATAL_ERROR
      "Could not inspect shared-library exports: ${_nm_error}")
  endif()
  string(REPLACE "\r\n" "\n" _nm_output "${_nm_output}")
  string(REPLACE "\n" ";" _nm_lines "${_nm_output}")
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
endif()

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
