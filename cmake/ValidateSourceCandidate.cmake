cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED MMXISF_SOURCE_DIR OR NOT DEFINED MMXISF_CANDIDATE_DIR)
  message(FATAL_ERROR
    "MMXISF_SOURCE_DIR and MMXISF_CANDIDATE_DIR are required")
endif()

set(_manifest_path "${MMXISF_CANDIDATE_DIR}/source-candidate.json")
if(NOT EXISTS "${_manifest_path}")
  message(FATAL_ERROR "Missing source-candidate manifest: ${_manifest_path}")
endif()
file(READ "${_manifest_path}" _manifest)

string(JSON _schema ERROR_VARIABLE _schema_error GET "${_manifest}"
  schemaVersion)
if(NOT _schema_error STREQUAL "NOTFOUND" OR
   NOT _schema STREQUAL "mmxisf.source-candidate/1.1.0")
  message(FATAL_ERROR "Invalid source-candidate schema")
endif()
string(JSON _status GET "${_manifest}" status)
if(NOT _status STREQUAL "PREPARED_NOT_PUBLISHED")
  message(FATAL_ERROR "Invalid source-candidate status: ${_status}")
endif()
string(JSON _publication_authorized GET "${_manifest}"
  publicationAuthorized)
if(_publication_authorized)
  message(FATAL_ERROR
    "A prepared source-candidate manifest cannot authorize publication")
endif()

file(READ "${MMXISF_SOURCE_DIR}/CMakeLists.txt" _cmake_source)
string(REGEX MATCH
  "project\\([\n\r\t ]*mmxisf[\n\r\t ]+VERSION[\n\r\t ]+([0-9]+\\.[0-9]+\\.[0-9]+)"
  _project_match "${_cmake_source}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "Cannot resolve mmxisf project version")
endif()
set(_expected_version "${CMAKE_MATCH_1}")
string(JSON _version GET "${_manifest}" version)
if(NOT _version STREQUAL _expected_version)
  message(FATAL_ERROR
    "Source-candidate version ${_version} does not match ${_expected_version}")
endif()

string(JSON _commit GET "${_manifest}" commit)
string(LENGTH "${_commit}" _commit_length)
if(NOT _commit_length EQUAL 40 OR NOT _commit MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR
    "Source-candidate commit is not a lowercase 40-hex object id")
endif()
if(DEFINED MMXISF_EXPECTED_COMMIT AND
   NOT _commit STREQUAL MMXISF_EXPECTED_COMMIT)
  message(FATAL_ERROR
    "Source-candidate commit ${_commit} does not match expected commit ${MMXISF_EXPECTED_COMMIT}")
endif()
string(SUBSTRING "${_commit}" 0 12 _short_commit)

string(JSON _archive_name GET "${_manifest}" archive name)
set(_expected_archive_name
  "mmxisf-${_version}-source-${_short_commit}.tar.gz")
if(NOT _archive_name STREQUAL _expected_archive_name)
  message(FATAL_ERROR
    "Source-candidate archive name ${_archive_name} does not match ${_expected_archive_name}")
endif()
string(JSON _archive_size GET "${_manifest}" archive sizeBytes)
string(JSON _archive_sha256 GET "${_manifest}" archive sha256)
string(JSON _archive_prefix GET "${_manifest}" archive prefix)
set(_expected_prefix "mmxisf-${_version}/")
if(NOT _archive_prefix STREQUAL _expected_prefix)
  message(FATAL_ERROR
    "Source-candidate archive prefix ${_archive_prefix} does not match ${_expected_prefix}")
endif()
if(NOT _archive_sha256 MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "Source-candidate archive SHA-256 is malformed")
endif()
string(LENGTH "${_archive_sha256}" _archive_sha256_length)
if(NOT _archive_sha256_length EQUAL 64)
  message(FATAL_ERROR "Source-candidate archive SHA-256 is malformed")
endif()

set(_archive_path "${MMXISF_CANDIDATE_DIR}/${_archive_name}")
if(NOT EXISTS "${_archive_path}")
  message(FATAL_ERROR "Missing source-candidate archive: ${_archive_path}")
endif()
file(SIZE "${_archive_path}" _actual_archive_size)
if(NOT _actual_archive_size EQUAL _archive_size)
  message(FATAL_ERROR
    "Archive size mismatch: manifest=${_archive_size}, actual=${_actual_archive_size}")
endif()
file(SHA256 "${_archive_path}" _actual_archive_sha256)
if(NOT _actual_archive_sha256 STREQUAL _archive_sha256)
  message(FATAL_ERROR
    "Archive SHA-256 mismatch: manifest=${_archive_sha256}, actual=${_actual_archive_sha256}")
endif()

set(_checksum_path "${_archive_path}.sha256")
if(NOT EXISTS "${_checksum_path}")
  message(FATAL_ERROR "Missing source-candidate checksum: ${_checksum_path}")
endif()
file(READ "${_checksum_path}" _checksum)
set(_expected_checksum "${_archive_sha256}  ${_archive_name}\n")
if(NOT _checksum STREQUAL _expected_checksum)
  message(FATAL_ERROR
    "Source-candidate checksum file does not exactly match the manifest")
endif()

set(_support_profile_path
  "${MMXISF_SOURCE_DIR}/docs/support-profile-0.1.0.json")
file(READ "${_support_profile_path}" _support_profile)
file(SHA256 "${_support_profile_path}" _actual_support_profile_sha256)
string(JSON _actual_support_profile_state GET "${_support_profile}" state)
string(JSON _actual_support_profile_ref GET "${_support_profile}" candidateRef)
string(JSON _manifest_support_profile_state GET "${_manifest}"
  supportProfile state)
string(JSON _manifest_support_profile_ref GET "${_manifest}"
  supportProfile candidateRef)
string(JSON _manifest_support_profile_sha256 GET "${_manifest}"
  supportProfile sha256)
if(NOT _manifest_support_profile_state STREQUAL _actual_support_profile_state OR
   NOT _manifest_support_profile_ref STREQUAL _actual_support_profile_ref OR
   NOT _manifest_support_profile_sha256 STREQUAL
     _actual_support_profile_sha256)
  message(FATAL_ERROR
    "Source-candidate support-profile binding does not match the source tree")
endif()

string(JSON _determinism GET "${_manifest}" determinismCheck)
if(NOT _determinism STREQUAL "PASS_TWO_BYTE_IDENTICAL_ARCHIVES")
  message(FATAL_ERROR
    "Invalid source-candidate determinism result: ${_determinism}")
endif()

message(STATUS
  "Source candidate verified: ${_archive_name} (${_archive_size} bytes, SHA-256 ${_archive_sha256})")
