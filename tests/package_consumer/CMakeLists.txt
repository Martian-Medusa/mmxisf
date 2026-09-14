cmake_minimum_required(VERSION 3.20)

project(mmxisf_package_consumer LANGUAGES CXX)

enable_testing()
find_package(mmxisf 0.1 REQUIRED CONFIG)
if(NOT DEFINED mmxisf_BINARY_SBOM OR NOT EXISTS "${mmxisf_BINARY_SBOM}")
  message(FATAL_ERROR
    "Installed mmxisf package did not export its binary dependency SBOM")
endif()

add_executable(mmxisf_package_consumer main.cpp)
target_link_libraries(mmxisf_package_consumer PRIVATE mmxisf::mmxisf)

add_test(NAME mmxisf.package-consumer COMMAND mmxisf_package_consumer)
