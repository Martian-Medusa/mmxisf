cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED MMXISF_SOURCE_DIR OR NOT DEFINED MMXISF_OUTPUT_DIR)
  message(FATAL_ERROR "MMXISF_SOURCE_DIR and MMXISF_OUTPUT_DIR are required")
endif()
if(EXISTS "${MMXISF_OUTPUT_DIR}")
  message(FATAL_ERROR
    "Refusing to overwrite candidate output directory: ${MMXISF_OUTPUT_DIR}")
endif()
if(NOT DEFINED MMXISF_CANDIDATE_REF)
  set(MMXISF_CANDIDATE_REF HEAD)
endif()

function(mmxisf_git output)
  execute_process(
    COMMAND git ${ARGN}
    WORKING_DIRECTORY "${MMXISF_SOURCE_DIR}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Git command failed (${ARGN}): ${_error}")
  endif()
  set(${output} "${_output}" PARENT_SCOPE)
endfunction()

mmxisf_git(_status status --porcelain=v1 --untracked-files=all)
if(NOT _status STREQUAL "")
  message(FATAL_ERROR "Source candidate requires a clean Git worktree")
endif()
mmxisf_git(_head rev-parse HEAD)
mmxisf_git(_commit rev-parse "${MMXISF_CANDIDATE_REF}^{commit}")
if(NOT _commit STREQUAL _head)
  message(FATAL_ERROR
    "Candidate ref must resolve to the checked-out HEAD (${_head}), got ${_commit}")
endif()
if(NOT _commit MATCHES "^[0-9a-f]{40}$")
  message(FATAL_ERROR "Candidate commit is not a lowercase 40-hex object id")
endif()
string(SUBSTRING "${_commit}" 0 12 _short_commit)

file(READ "${MMXISF_SOURCE_DIR}/CMakeLists.txt" _cmake_source)
string(REGEX MATCH
  "project\\([\n\r\t ]*mmxisf[\n\r\t ]+VERSION[\n\r\t ]+([0-9]+\\.[0-9]+\\.[0-9]+)"
  _project_match "${_cmake_source}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "Cannot resolve mmxisf project version")
endif()
set(_version "${CMAKE_MATCH_1}")

file(MAKE_DIRECTORY "${MMXISF_OUTPUT_DIR}")
set(_archive_name "mmxisf-${_version}-source-${_short_commit}.tar.gz")
set(_archive "${MMXISF_OUTPUT_DIR}/${_archive_name}")
set(_repeat "${MMXISF_OUTPUT_DIR}/.${_archive_name}.repeat")
set(_prefix "mmxisf-${_version}/")

foreach(_output IN ITEMS "${_archive}" "${_repeat}")
  execute_process(
    COMMAND git archive --format=tar.gz "--prefix=${_prefix}"
      "--output=${_output}" "${_commit}"
    WORKING_DIRECTORY "${MMXISF_SOURCE_DIR}"
    RESULT_VARIABLE _archive_result
    ERROR_VARIABLE _archive_error
  )
  if(NOT _archive_result EQUAL 0)
    message(FATAL_ERROR "Could not create source archive: ${_archive_error}")
  endif()
endforeach()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${_archive}" "${_repeat}"
  RESULT_VARIABLE _compare_result
)
if(NOT _compare_result EQUAL 0)
  message(FATAL_ERROR "Repeated source archives are not byte-identical")
endif()
file(REMOVE "${_repeat}")

file(SHA256 "${_archive}" _archive_sha256)
file(SIZE "${_archive}" _archive_size)
file(WRITE "${_archive}.sha256" "${_archive_sha256}  ${_archive_name}\n")
file(WRITE "${MMXISF_OUTPUT_DIR}/source-candidate.json"
  "{\n"
  "  \"schemaVersion\": \"mmxisf.source-candidate/1.0.0\",\n"
  "  \"status\": \"PREPARED_NOT_PUBLISHED\",\n"
  "  \"publicationAuthorized\": false,\n"
  "  \"version\": \"${_version}\",\n"
  "  \"commit\": \"${_commit}\",\n"
  "  \"archive\": {\n"
  "    \"name\": \"${_archive_name}\",\n"
  "    \"sizeBytes\": ${_archive_size},\n"
  "    \"sha256\": \"${_archive_sha256}\",\n"
  "    \"prefix\": \"${_prefix}\"\n"
  "  },\n"
  "  \"determinismCheck\": \"PASS_TWO_BYTE_IDENTICAL_ARCHIVES\"\n"
  "}\n")

message(STATUS
  "Prepared unpublished source candidate ${_archive_name} (${_archive_size} bytes, SHA-256 ${_archive_sha256})")
