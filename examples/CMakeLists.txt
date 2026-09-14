# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(mmxisf_examples LANGUAGES CXX)
  find_package(mmxisf 0.1 CONFIG REQUIRED)
endif()

add_executable(mmxisf_example_read read_image.cpp)
set_target_properties(mmxisf_example_read
  PROPERTIES OUTPUT_NAME "mmxisf-example-read")
target_link_libraries(mmxisf_example_read PRIVATE mmxisf::mmxisf)

add_executable(mmxisf_example_write write_image.cpp)
set_target_properties(mmxisf_example_write
  PROPERTIES OUTPUT_NAME "mmxisf-example-write")
target_link_libraries(mmxisf_example_write PRIVATE mmxisf::mmxisf)
