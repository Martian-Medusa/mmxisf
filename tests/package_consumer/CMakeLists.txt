cmake_minimum_required(VERSION 3.20)

project(mmxisf_package_consumer LANGUAGES CXX)

enable_testing()
find_package(mmxisf 0.1 REQUIRED CONFIG)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPublicHeaderSelfContainment.cmake")
if(NOT DEFINED mmxisf_BINARY_SBOM OR NOT EXISTS "${mmxisf_BINARY_SBOM}")
  message(FATAL_ERROR
    "Installed mmxisf package did not export its binary dependency SBOM")
endif()
foreach(_contract_variable
    mmxisf_SUPPORT_PROFILE
    mmxisf_CONFORMANCE_MATRIX
    mmxisf_SPECIFICATION_BASELINE)
  if(NOT DEFINED ${_contract_variable} OR
     NOT EXISTS "${${_contract_variable}}")
    message(FATAL_ERROR
      "Installed mmxisf package did not export ${_contract_variable}")
  endif()
endforeach()

file(READ "${mmxisf_SUPPORT_PROFILE}" _support_profile)
string(JSON _support_profile_schema GET "${_support_profile}" schema)
if(NOT _support_profile_schema STREQUAL "mmxisf.support-profile/1.0.0")
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
mmxisf_add_public_header_self_containment_test(
  mmxisf_installed_public_headers_self_containment
  "${CMAKE_CURRENT_LIST_DIR}/../../include/mmxisf")

add_test(NAME mmxisf.package-consumer COMMAND mmxisf_package_consumer)
