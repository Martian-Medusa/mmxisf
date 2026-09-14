# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

project(mmxisf_subdirectory_consumer LANGUAGES CXX)

if(NOT DEFINED MMXISF_SOURCE_DIR OR
   NOT EXISTS "${MMXISF_SOURCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "MMXISF_SOURCE_DIR must name an mmxisf source tree")
endif()

add_subdirectory("${MMXISF_SOURCE_DIR}" mmxisf-source EXCLUDE_FROM_ALL)

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

enable_testing()
add_test(NAME mmxisf.subdirectory-consumer
  COMMAND mmxisf_subdirectory_consumer)
