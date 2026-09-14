# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

project(mmxisf_subdirectory_consumer LANGUAGES CXX)

if(NOT DEFINED MMXISF_SOURCE_DIR OR
   NOT EXISTS "${MMXISF_SOURCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "MMXISF_SOURCE_DIR must name an mmxisf source tree")
endif()

if(DEFINED BUILD_TESTING)
  set(_consumer_had_build_testing true)
else()
  set(_consumer_had_build_testing false)
endif()
add_subdirectory("${MMXISF_SOURCE_DIR}" mmxisf-source EXCLUDE_FROM_ALL)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPublicHeaderSelfContainment.cmake")

if(NOT _consumer_had_build_testing AND DEFINED BUILD_TESTING)
  message(FATAL_ERROR
    "Embedded mmxisf leaked the global BUILD_TESTING option into its consumer")
endif()

foreach(_unexpected_target IN ITEMS
    mmxisf_version_test
    mmxisf_public_api_contract_test
    mmxisf_reader_test
    mmxisf_writer_test
    mmxisf_independent_producer_test
    mmxisf_inspect
    mmxisf_writer_benchmark
    mmxisf_viewer
    mmxisf_viewer_preview
    mmxisf_fuzz_header
    mmxisf_fuzz_smoke
    mmxisf_docs)
  if(TARGET ${_unexpected_target})
    message(FATAL_ERROR
      "Embedded mmxisf created developer target ${_unexpected_target}")
  endif()
endforeach()

add_executable(mmxisf_subdirectory_consumer main.cpp)
target_link_libraries(mmxisf_subdirectory_consumer PRIVATE mmxisf::mmxisf)
mmxisf_add_public_header_self_containment_test(
  mmxisf_embedded_public_headers_self_containment
  "${MMXISF_SOURCE_DIR}/include/mmxisf")

enable_testing()
add_test(NAME mmxisf.subdirectory-consumer
  COMMAND mmxisf_subdirectory_consumer)
