cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED MMXISF_SOURCE_DIR OR NOT DEFINED MMXISF_BINARY_DIR)
  message(FATAL_ERROR "MMXISF_SOURCE_DIR and MMXISF_BINARY_DIR are required")
endif()

set(_validator "${MMXISF_SOURCE_DIR}/cmake/ValidateSourceCandidate.cmake")
set(_contract_root "${MMXISF_BINARY_DIR}/source-candidate-contract")
file(REMOVE_RECURSE "${_contract_root}")
file(MAKE_DIRECTORY "${_contract_root}")

set(_commit "1111111111111111111111111111111111111111")
set(_archive_name "mmxisf-0.1.0-source-111111111111.tar.gz")
set(_archive_content "deterministic source candidate fixture\n")
string(SHA256 _archive_sha256 "${_archive_content}")
string(LENGTH "${_archive_content}" _archive_size)
file(SHA256 "${MMXISF_SOURCE_DIR}/docs/support-profile-0.1.0.json"
  _support_profile_sha256)
file(READ "${MMXISF_SOURCE_DIR}/docs/support-profile-0.1.0.json"
  _support_profile)
string(JSON _support_profile_state GET "${_support_profile}" state)
string(JSON _support_profile_ref GET "${_support_profile}" candidateRef)

string(CONCAT _valid_manifest
  "{\n"
  "  \"schemaVersion\": \"mmxisf.source-candidate/1.1.0\",\n"
  "  \"status\": \"PREPARED_NOT_PUBLISHED\",\n"
  "  \"publicationAuthorized\": false,\n"
  "  \"version\": \"0.1.0\",\n"
  "  \"commit\": \"${_commit}\",\n"
  "  \"archive\": {\n"
  "    \"name\": \"${_archive_name}\",\n"
  "    \"sizeBytes\": ${_archive_size},\n"
  "    \"sha256\": \"${_archive_sha256}\",\n"
  "    \"prefix\": \"mmxisf-0.1.0/\"\n"
  "  },\n"
  "  \"supportProfile\": {\n"
  "    \"state\": \"${_support_profile_state}\",\n"
  "    \"candidateRef\": \"${_support_profile_ref}\",\n"
  "    \"sha256\": \"${_support_profile_sha256}\"\n"
  "  },\n"
  "  \"determinismCheck\": \"PASS_TWO_BYTE_IDENTICAL_ARCHIVES\"\n"
  "}\n")

function(mmxisf_write_candidate_fixture name manifest)
  set(_dir "${_contract_root}/${name}")
  file(MAKE_DIRECTORY "${_dir}")
  file(WRITE "${_dir}/${_archive_name}" "${_archive_content}")
  file(WRITE "${_dir}/${_archive_name}.sha256"
    "${_archive_sha256}  ${_archive_name}\n")
  file(WRITE "${_dir}/source-candidate.json" "${manifest}")
endfunction()

function(mmxisf_run_candidate_validator name result_var output_var)
  execute_process(
    COMMAND
      "${CMAKE_COMMAND}"
      "-DMMXISF_SOURCE_DIR=${MMXISF_SOURCE_DIR}"
      "-DMMXISF_CANDIDATE_DIR=${_contract_root}/${name}"
      "-DMMXISF_EXPECTED_COMMIT=${_commit}"
      -P "${_validator}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error
  )
  string(CONCAT _diagnostic "${_output}" "${_error}")
  set(${result_var} "${_result}" PARENT_SCOPE)
  set(${output_var} "${_diagnostic}" PARENT_SCOPE)
endfunction()

mmxisf_write_candidate_fixture(valid "${_valid_manifest}")
mmxisf_run_candidate_validator(valid _valid_result _valid_diagnostic)
if(NOT _valid_result EQUAL 0)
  message(FATAL_ERROR
    "Source-candidate validator rejected valid fixture: ${_valid_diagnostic}")
endif()

function(mmxisf_expect_candidate_rejection name manifest expected)
  mmxisf_write_candidate_fixture("${name}" "${manifest}")
  mmxisf_run_candidate_validator("${name}" _result _diagnostic)
  if(_result EQUAL 0)
    message(FATAL_ERROR
      "Source-candidate validator accepted invalid case ${name}")
  endif()
  if(NOT _diagnostic MATCHES "${expected}")
    message(FATAL_ERROR
      "Source-candidate case ${name} missed expected diagnostic ${expected}: ${_diagnostic}")
  endif()
endfunction()

string(REPLACE
  "\"publicationAuthorized\": false"
  "\"publicationAuthorized\": true"
  _published_manifest "${_valid_manifest}")
mmxisf_expect_candidate_rejection(publication-authorized
  "${_published_manifest}" "cannot authorize publication")

string(REPLACE
  "\"commit\": \"${_commit}\""
  "\"commit\": \"2222222222222222222222222222222222222222\""
  _wrong_commit_manifest "${_valid_manifest}")
mmxisf_expect_candidate_rejection(wrong-commit
  "${_wrong_commit_manifest}" "expected commit")

string(REPLACE
  "\"name\": \"${_archive_name}\""
  "\"name\": \"../candidate.tar.gz\""
  _unsafe_name_manifest "${_valid_manifest}")
mmxisf_expect_candidate_rejection(unsafe-name
  "${_unsafe_name_manifest}" "archive name .* does not match")

string(REPLACE
  "\"sha256\": \"${_archive_sha256}\""
  "\"sha256\": \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\""
  _wrong_archive_hash_manifest "${_valid_manifest}")
mmxisf_expect_candidate_rejection(wrong-archive-hash
  "${_wrong_archive_hash_manifest}" "Archive SHA-256 mismatch")

string(REPLACE
  "\"sha256\": \"${_support_profile_sha256}\""
  "\"sha256\": \"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\""
  _wrong_profile_hash_manifest "${_valid_manifest}")
mmxisf_expect_candidate_rejection(wrong-profile-hash
  "${_wrong_profile_hash_manifest}" "support-profile binding does not match")

message(STATUS "Source-candidate manifest contracts: PASS")
