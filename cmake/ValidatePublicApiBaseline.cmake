cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED MMXISF_SOURCE_DIR)
  message(FATAL_ERROR "MMXISF_SOURCE_DIR is required")
endif()
if(DEFINED MMXISF_PUBLIC_API_BASELINE_FILE)
  set(_baseline_path "${MMXISF_PUBLIC_API_BASELINE_FILE}")
else()
  set(_baseline_path
    "${MMXISF_SOURCE_DIR}/docs/public-api-baseline-0.1.0.json")
endif()
if(NOT EXISTS "${_baseline_path}")
  message(FATAL_ERROR "Missing public API baseline: ${_baseline_path}")
endif()

file(READ "${_baseline_path}" _baseline)
string(JSON _schema ERROR_VARIABLE _schema_error GET "${_baseline}" schema)
if(NOT _schema_error STREQUAL "NOTFOUND" OR
   NOT _schema STREQUAL "mmxisf.public-api-baseline/1.0.0")
  message(FATAL_ERROR "Invalid public API baseline schema")
endif()

file(READ "${MMXISF_SOURCE_DIR}/CMakeLists.txt" _cmake_source)
string(REGEX MATCH
  "project\\([\n\r\t ]*mmxisf[\n\r\t ]+VERSION[\n\r\t ]+([0-9]+\\.[0-9]+\\.[0-9]+)"
  _project_match "${_cmake_source}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "Cannot resolve mmxisf project version")
endif()
string(JSON _baseline_version GET "${_baseline}" projectVersion)
if(NOT _baseline_version STREQUAL CMAKE_MATCH_1)
  message(FATAL_ERROR
    "Public API baseline version ${_baseline_version} does not match project version ${CMAKE_MATCH_1}")
endif()

string(JSON _audited_commit GET "${_baseline}" auditedCommit)
string(LENGTH "${_audited_commit}" _audited_commit_length)
if(NOT _audited_commit_length EQUAL 40 OR
   NOT _audited_commit MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "Public API audited commit must be lowercase 40-hex")
endif()
string(JSON _abi_policy GET "${_baseline}" abiPolicy)
if(NOT _abi_policy STREQUAL "NO_ABI_PROMISE_BEFORE_1.0")
  message(FATAL_ERROR "Unexpected public API ABI policy: ${_abi_policy}")
endif()

file(GLOB _actual_header_paths RELATIVE "${MMXISF_SOURCE_DIR}"
  "${MMXISF_SOURCE_DIR}/include/mmxisf/*.hpp")
list(SORT _actual_header_paths)
string(JSON _baseline_header_count LENGTH "${_baseline}" headers)
list(LENGTH _actual_header_paths _actual_header_count)
if(NOT _baseline_header_count EQUAL _actual_header_count)
  message(FATAL_ERROR
    "Public API baseline has ${_baseline_header_count} headers; source has ${_actual_header_count}")
endif()

set(_baseline_header_paths)
if(_baseline_header_count GREATER 0)
  math(EXPR _last_header "${_baseline_header_count} - 1")
  foreach(_index RANGE ${_last_header})
    string(JSON _path GET "${_baseline}" headers ${_index} path)
    string(JSON _expected_sha256 GET "${_baseline}" headers ${_index}
      sha256)
    if(NOT _path MATCHES "^include/mmxisf/[a-z0-9_]+\\.hpp$")
      message(FATAL_ERROR "Invalid public API header path: ${_path}")
    endif()
    list(FIND _baseline_header_paths "${_path}" _duplicate_index)
    if(NOT _duplicate_index EQUAL -1)
      message(FATAL_ERROR "Duplicate public API header path: ${_path}")
    endif()
    list(APPEND _baseline_header_paths "${_path}")
    list(FIND _actual_header_paths "${_path}" _actual_index)
    if(_actual_index EQUAL -1)
      message(FATAL_ERROR "Unknown public API baseline header: ${_path}")
    endif()
    string(LENGTH "${_expected_sha256}" _sha256_length)
    if(NOT _sha256_length EQUAL 64 OR
       NOT _expected_sha256 MATCHES "^[0-9a-f]+$")
      message(FATAL_ERROR "Malformed public API SHA-256 for ${_path}")
    endif()
    file(SHA256 "${MMXISF_SOURCE_DIR}/${_path}" _actual_sha256)
    if(NOT _actual_sha256 STREQUAL _expected_sha256)
      message(FATAL_ERROR "Public API header SHA-256 mismatch: ${_path}")
    endif()
  endforeach()
endif()
list(SORT _baseline_header_paths)
if(NOT _baseline_header_paths STREQUAL _actual_header_paths)
  message(FATAL_ERROR
    "Public API baseline header set does not match the source header set")
endif()

message(STATUS
  "Validated ${_actual_header_count} public API headers against audit ${_audited_commit}")
