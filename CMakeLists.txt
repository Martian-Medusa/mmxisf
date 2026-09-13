cmake_minimum_required(VERSION 3.20)

project(
  mmxisf
  VERSION 0.1.0
  DESCRIPTION "Standalone C++ foundation for a clean-room XISF implementation"
  LANGUAGES CXX
)

include(CTest)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(MMXISF_BUILD_TESTS "Build mmxisf tests" ${BUILD_TESTING})
option(MMXISF_BUILD_TOOLS "Build mmxisf command-line tools" ON)
option(MMXISF_BUILD_VIEWER "Build the macOS PoC viewer" OFF)
option(MMXISF_BUILD_FUZZER "Build the Clang libFuzzer harness" OFF)
option(MMXISF_BUILD_FUZZ_SMOKE "Build the deterministic sanitizer fuzz smoke" OFF)
option(BUILD_SHARED_LIBS "Build mmxisf as a shared library" OFF)
set(
  MMXISF_VIEWER_OUTPUT_DIR
  "${PROJECT_SOURCE_DIR}/artifacts"
  CACHE PATH
  "Output directory for the macOS PoC viewer bundle"
)

find_package(EXPAT 2.2 REQUIRED)
find_package(LZ4 REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(ZLIB REQUIRED)
find_package(ZSTD REQUIRED)

add_library(mmxisf src/reader.cpp src/version.cpp src/writer.cpp)
add_library(mmxisf::mmxisf ALIAS mmxisf)

if(BUILD_SHARED_LIBS)
  target_compile_definitions(mmxisf PRIVATE MMXISF_BUILDING_LIBRARY)
else()
  target_compile_definitions(mmxisf PUBLIC MMXISF_STATIC_DEFINE)
endif()

target_compile_features(mmxisf PUBLIC cxx_std_20)
target_include_directories(
  mmxisf
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(
  mmxisf
  PRIVATE EXPAT::EXPAT LZ4::LZ4 OpenSSL::Crypto ZLIB::ZLIB ZSTD::ZSTD
)

if(MMXISF_BUILD_TESTS OR MMXISF_BUILD_VIEWER)
  add_library(mmxisf_viewer_preview STATIC viewer/Preview.cpp)
  target_compile_features(mmxisf_viewer_preview PUBLIC cxx_std_20)
  target_include_directories(
    mmxisf_viewer_preview
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/viewer"
  )
  target_link_libraries(mmxisf_viewer_preview PUBLIC mmxisf::mmxisf)
endif()

if(MMXISF_BUILD_FUZZER OR MMXISF_BUILD_FUZZ_SMOKE)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "MMXISF_BUILD_FUZZER currently requires Clang")
  endif()
  target_compile_options(
    mmxisf
    PRIVATE
      -fsanitize=fuzzer-no-link,address,undefined
      -fno-omit-frame-pointer
  )
  target_link_options(mmxisf INTERFACE -fsanitize=address,undefined)
endif()

set_target_properties(
  mmxisf
  PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)

if(MMXISF_BUILD_TESTS)
  add_executable(mmxisf_version_test tests/version_test.cpp)
  target_link_libraries(mmxisf_version_test PRIVATE mmxisf::mmxisf)
  add_test(NAME mmxisf.version COMMAND mmxisf_version_test)
  add_executable(mmxisf_reader_test tests/reader_test.cpp)
  target_link_libraries(mmxisf_reader_test PRIVATE mmxisf::mmxisf)
  add_test(NAME mmxisf.reader COMMAND mmxisf_reader_test)
  add_executable(mmxisf_writer_test tests/writer_test.cpp)
  target_link_libraries(mmxisf_writer_test
    PRIVATE mmxisf::mmxisf OpenSSL::Crypto)
  add_test(NAME mmxisf.writer COMMAND mmxisf_writer_test)
  add_executable(mmxisf_independent_producer_test
    tests/independent_producer_test.cpp)
  target_compile_definitions(mmxisf_independent_producer_test
    PRIVATE MMXISF_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(mmxisf_independent_producer_test
    PRIVATE mmxisf::mmxisf OpenSSL::Crypto)
  add_test(NAME mmxisf.independent-producer
    COMMAND mmxisf_independent_producer_test)
  add_executable(mmxisf_viewer_preview_test tests/viewer_preview_test.cpp)
  target_link_libraries(
    mmxisf_viewer_preview_test PRIVATE mmxisf_viewer_preview
  )
  add_test(NAME mmxisf.viewer-preview COMMAND mmxisf_viewer_preview_test)
  add_test(
    NAME mmxisf.planning
    COMMAND
      ${CMAKE_COMMAND}
      -DMMXISF_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ValidatePlanning.cmake
  )
  add_test(
    NAME mmxisf.sbom
    COMMAND
      ${CMAKE_COMMAND}
      -DMMXISF_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -DMMXISF_PROJECT_VERSION=${PROJECT_VERSION}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ValidateSbom.cmake
  )
endif()

if(MMXISF_BUILD_TOOLS)
  add_executable(mmxisf_inspect tools/mmxisf_inspect.cpp)
  set_target_properties(mmxisf_inspect PROPERTIES OUTPUT_NAME "mmxisf-inspect")
  target_link_libraries(mmxisf_inspect PRIVATE mmxisf::mmxisf OpenSSL::Crypto)
  add_executable(mmxisf_writer_benchmark tools/mmxisf_writer_benchmark.cpp)
  set_target_properties(mmxisf_writer_benchmark
    PROPERTIES OUTPUT_NAME "mmxisf-writer-benchmark")
  target_link_libraries(mmxisf_writer_benchmark PRIVATE mmxisf::mmxisf)
endif()

if(MMXISF_BUILD_FUZZER)
  add_executable(mmxisf_fuzz_header tests/fuzz_header.cpp)
  target_compile_options(
    mmxisf_fuzz_header
    PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
  )
  target_link_options(
    mmxisf_fuzz_header
    PRIVATE -fsanitize=fuzzer,address,undefined
  )
  target_link_libraries(mmxisf_fuzz_header PRIVATE mmxisf::mmxisf)
endif()

if(MMXISF_BUILD_FUZZ_SMOKE)
  add_executable(mmxisf_fuzz_smoke tests/fuzz_header.cpp)
  target_compile_definitions(mmxisf_fuzz_smoke PRIVATE MMXISF_FUZZ_SMOKE_MAIN=1)
  target_compile_options(
    mmxisf_fuzz_smoke
    PRIVATE
      -fsanitize=address,undefined
      -fno-omit-frame-pointer
  )
  target_link_options(mmxisf_fuzz_smoke PRIVATE -fsanitize=address,undefined)
  target_link_libraries(mmxisf_fuzz_smoke PRIVATE mmxisf::mmxisf)
endif()

if(MMXISF_BUILD_VIEWER)
  if(NOT APPLE)
    message(FATAL_ERROR "MMXISF_BUILD_VIEWER is currently supported only on macOS")
  endif()
  enable_language(OBJCXX)
  add_subdirectory(viewer)
endif()

configure_package_config_file(
  cmake/mmxisfConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/mmxisfConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/mmxisf"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/mmxisfConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(
  TARGETS mmxisf
  EXPORT mmxisfTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(MMXISF_BUILD_TOOLS)
  install(TARGETS mmxisf_inspect RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
  FILES LICENSE NOTICE SECURITY.md CONTRIBUTING.md THIRD_PARTY_NOTICES.md
  DESTINATION "${CMAKE_INSTALL_DATADIR}/mmxisf"
)
install(
  FILES sbom/source-dependencies.spdx.json
  DESTINATION "${CMAKE_INSTALL_DATADIR}/mmxisf/sbom"
)
install(
  EXPORT mmxisfTargets
  FILE mmxisfTargets.cmake
  NAMESPACE mmxisf::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/mmxisf"
)
install(
  FILES
    cmake/FindLZ4.cmake
    cmake/FindZSTD.cmake
    "${CMAKE_CURRENT_BINARY_DIR}/mmxisfConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/mmxisfConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/mmxisf"
)
