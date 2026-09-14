cmake_minimum_required(VERSION 3.20)

project(mmxisf_package_consumer LANGUAGES CXX)

enable_testing()
find_package(mmxisf 0.1 REQUIRED CONFIG)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPublicHeaderSelfContainment.cmake")
if(NOT DEFINED mmxisf_LINKAGE OR
   NOT mmxisf_LINKAGE MATCHES "^(static|shared)$")
  message(FATAL_ERROR "Installed mmxisf package did not export its linkage")
endif()
if(DEFINED MMXISF_EXPECTED_LINKAGE AND
   NOT mmxisf_LINKAGE STREQUAL MMXISF_EXPECTED_LINKAGE)
  message(FATAL_ERROR
    "Installed mmxisf linkage is ${mmxisf_LINKAGE}; expected "
    "${MMXISF_EXPECTED_LINKAGE}")
endif()
if(NOT DEFINED mmxisf_BINARY_SBOM OR NOT EXISTS "${mmxisf_BINARY_SBOM}")
  message(FATAL_ERROR
    "Installed mmxisf package did not export its binary dependency SBOM")
endif()
foreach(_contract_variable
    mmxisf_SUPPORT_PROFILE
    mmxisf_CONFORMANCE_MATRIX
    mmxisf_SPECIFICATION_BASELINE
    mmxisf_EXAMPLES_DIR)
  if(NOT DEFINED ${_contract_variable} OR
     NOT EXISTS "${${_contract_variable}}")
    message(FATAL_ERROR
      "Installed mmxisf package did not export ${_contract_variable}")
  endif()
endforeach()

if(DEFINED MMXISF_EXPECTED_PACKAGE_PREFIX)
  file(REAL_PATH "${MMXISF_EXPECTED_PACKAGE_PREFIX}"
    _expected_package_prefix)

  function(mmxisf_require_path_in_expected_prefix path description)
    file(REAL_PATH "${path}" _actual_path)
    string(FIND "${_actual_path}" "${_expected_package_prefix}/"
      _prefix_position)
    if(NOT _prefix_position EQUAL 0)
      message(FATAL_ERROR
        "${description} resolved outside the expected package prefix: "
        "${_actual_path} (expected ${_expected_package_prefix})")
    endif()
  endfunction()

  mmxisf_require_path_in_expected_prefix(
    "${mmxisf_BINARY_SBOM}" "Binary SBOM")
  mmxisf_require_path_in_expected_prefix(
    "${mmxisf_SUPPORT_PROFILE}" "Support profile")
  mmxisf_require_path_in_expected_prefix(
    "${mmxisf_CONFORMANCE_MATRIX}" "Conformance matrix")
  mmxisf_require_path_in_expected_prefix(
    "${mmxisf_SPECIFICATION_BASELINE}" "Specification baseline")
  mmxisf_require_path_in_expected_prefix(
    "${mmxisf_EXAMPLES_DIR}" "Public API examples")

  get_target_property(_imported_configurations mmxisf::mmxisf
    IMPORTED_CONFIGURATIONS)
  if(NOT _imported_configurations)
    set(_imported_configurations NOCONFIG)
  endif()
  set(_checked_imported_artifact false)
  foreach(_configuration IN LISTS _imported_configurations)
    foreach(_artifact_property IN ITEMS
        IMPORTED_LOCATION_${_configuration}
        IMPORTED_IMPLIB_${_configuration})
      get_target_property(_artifact mmxisf::mmxisf ${_artifact_property})
      if(_artifact AND NOT _artifact MATCHES "-NOTFOUND$")
        mmxisf_require_path_in_expected_prefix(
          "${_artifact}" "Imported mmxisf artifact")
        set(_checked_imported_artifact true)
      endif()
    endforeach()
  endforeach()
  if(NOT _checked_imported_artifact)
    message(FATAL_ERROR "Installed mmxisf target exports no package artifact")
  endif()
endif()

file(READ "${mmxisf_SUPPORT_PROFILE}" _support_profile)
string(JSON _support_profile_schema GET "${_support_profile}" schema)
if(NOT _support_profile_schema STREQUAL "mmxisf.support-profile/1.1.0")
  message(FATAL_ERROR "Installed mmxisf support profile has wrong schema")
endif()
string(JSON _viewer_included GET "${_support_profile}"
  productBoundary viewerIncluded)
if(_viewer_included)
  message(FATAL_ERROR
    "Installed standalone support profile includes the optional viewer")
endif()
string(JSON _expected_matrix_sha GET "${_support_profile}" matrix sha256)
file(SHA256 "${mmxisf_CONFORMANCE_MATRIX}" _actual_matrix_sha)
if(NOT _actual_matrix_sha STREQUAL _expected_matrix_sha)
  message(FATAL_ERROR
    "Installed support profile and conformance matrix do not match")
endif()

add_executable(mmxisf_package_consumer main.cpp)
target_link_libraries(mmxisf_package_consumer PRIVATE mmxisf::mmxisf)
add_executable(mmxisf_package_example_read
  "${mmxisf_EXAMPLES_DIR}/read_image.cpp")
target_link_libraries(mmxisf_package_example_read PRIVATE mmxisf::mmxisf)
add_executable(mmxisf_package_example_write
  "${mmxisf_EXAMPLES_DIR}/write_image.cpp")
target_link_libraries(mmxisf_package_example_write PRIVATE mmxisf::mmxisf)
mmxisf_add_public_header_self_containment_test(
  mmxisf_installed_public_headers_self_containment
  "${CMAKE_CURRENT_LIST_DIR}/../../include/mmxisf")

add_test(NAME mmxisf.package-consumer COMMAND mmxisf_package_consumer)
add_test(
  NAME mmxisf.package-examples
  COMMAND
    ${CMAKE_COMMAND}
    -DMMXISF_READ_EXAMPLE=$<TARGET_FILE:mmxisf_package_example_read>
    -DMMXISF_WRITE_EXAMPLE=$<TARGET_FILE:mmxisf_package_example_write>
    -DMMXISF_EXAMPLE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mmxisf-package-example.xisf
    -P ${CMAKE_CURRENT_LIST_DIR}/../cmake/RunExamples.cmake
)
