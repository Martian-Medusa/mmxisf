cmake_minimum_required(VERSION 3.20)

project(
  mmxisf
  VERSION 0.1.0
  DESCRIPTION "Standalone C++ foundation for a clean-room XISF implementation"
  LANGUAGES CXX
)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(MMXISF_DEFAULT_BUILD_TESTS OFF)
set(MMXISF_DEFAULT_BUILD_TOOLS OFF)
set(MMXISF_DEFAULT_BUILD_EXAMPLES OFF)
set(MMXISF_DEFAULT_INSTALL OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  include(CTest)
  set(MMXISF_DEFAULT_BUILD_TESTS ${BUILD_TESTING})
  set(MMXISF_DEFAULT_BUILD_TOOLS ON)
  set(MMXISF_DEFAULT_BUILD_EXAMPLES ON)
  set(MMXISF_DEFAULT_INSTALL ON)
endif()

set(MMXISF_INSTALL_SBOM_DIR "${CMAKE_INSTALL_DATADIR}/mmxisf/sbom")
set(MMXISF_INSTALL_EXAMPLES_DIR
  "${CMAKE_INSTALL_DATADIR}/doc/mmxisf/examples")
set(
  MMXISF_INSTALL_CONFORMANCE_DIR
  "${CMAKE_INSTALL_DATADIR}/mmxisf/conformance"
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(MMXISF_BUILD_TESTS "Build mmxisf tests" ${MMXISF_DEFAULT_BUILD_TESTS})
option(MMXISF_BUILD_TOOLS "Build mmxisf command-line tools"
  ${MMXISF_DEFAULT_BUILD_TOOLS})
option(MMXISF_BUILD_EXAMPLES "Build mmxisf public API examples"
  ${MMXISF_DEFAULT_BUILD_EXAMPLES})
option(MMXISF_BUILD_VIEWER "Build the macOS PoC viewer" OFF)
option(MMXISF_BUILD_FUZZER "Build the Clang libFuzzer harness" OFF)
option(MMXISF_BUILD_FUZZ_SMOKE "Build the deterministic sanitizer fuzz smoke" OFF)
option(MMXISF_BUILD_DOCS "Build the generated mmxisf API reference" OFF)
option(MMXISF_INSTALL "Add mmxisf installation and package rules"
  ${MMXISF_DEFAULT_INSTALL})
option(
  MMXISF_ENFORCE_PRODUCTION_DEPENDENCY_BASELINE
  "Reject dependencies below the dated production security baseline"
  OFF
)
option(BUILD_SHARED_LIBS "Build mmxisf as a shared library" OFF)
set(
  MMXISF_VIEWER_OUTPUT_DIR
  "${PROJECT_SOURCE_DIR}/artifacts"
  CACHE PATH
  "Output directory for the macOS PoC viewer bundle"
)

find_package(EXPAT 2.2 REQUIRED)
find_package(LZ4 REQUIRED)
find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
find_package(ZLIB REQUIRED)
find_package(ZSTD REQUIRED)

include(cmake/ResolveDependencyVersions.cmake)
if(MMXISF_ENFORCE_PRODUCTION_DEPENDENCY_BASELINE)
  include(cmake/EnforceProductionDependencyBaseline.cmake)
endif()
string(TIMESTAMP MMXISF_SBOM_CREATED "%Y-%m-%dT%H:%M:%SZ" UTC)
if(BUILD_SHARED_LIBS)
  set(MMXISF_BINARY_LINKAGE "shared")
else()
  set(MMXISF_BINARY_LINKAGE "static")
endif()
if(CMAKE_BUILD_TYPE)
  set(MMXISF_BUILD_CONFIGURATION "${CMAKE_BUILD_TYPE}")
else()
  set(MMXISF_BUILD_CONFIGURATION "multi-config")
endif()
string(
  SHA256 MMXISF_BINARY_SBOM_ID
  "${PROJECT_VERSION};${CMAKE_SYSTEM_NAME};${CMAKE_SYSTEM_PROCESSOR};${CMAKE_CXX_COMPILER_ID};${CMAKE_CXX_COMPILER_VERSION};${MMXISF_BUILD_CONFIGURATION};${MMXISF_BINARY_LINKAGE};${MMXISF_EXPAT_VERSION};${MMXISF_ZLIB_VERSION};${MMXISF_LZ4_VERSION};${MMXISF_ZSTD_VERSION};${MMXISF_OPENSSL_VERSION};${MMXISF_SBOM_CREATED}"
)
configure_file(
  sbom/binary-dependencies.spdx.json.in
  "${CMAKE_CURRENT_BINARY_DIR}/binary-dependencies.spdx.json"
  @ONLY
)

if(MMXISF_BUILD_DOCS)
  find_package(Doxygen 1.9 REQUIRED)
  configure_file(
    docs/Doxyfile.in
    "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
    @ONLY
  )
  add_custom_target(
    mmxisf_docs ALL
    COMMAND Doxygen::doxygen "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating mmxisf API reference"
    VERBATIM
  )
endif()

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
if(BUILD_SHARED_LIBS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # vcpkg commonly supplies static dependency archives even when mmxisf itself
  # is shared. Do not re-export their C entry points or weak standard-library
  # template instantiations from libmmxisf.so.
  target_link_options(
    mmxisf
    PRIVATE
      "LINKER:--exclude-libs,ALL"
      "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/mmxisf-exports.map"
  )
  set_property(
    TARGET mmxisf APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/mmxisf-exports.map"
  )
elseif(BUILD_SHARED_LIBS AND APPLE)
  # vcpkg supplies static dependency archives. Without an explicit export
  # list, ld64 promotes thousands of their symbols into the public dylib.
  target_link_options(
    mmxisf
    PRIVATE
      "LINKER:-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/cmake/mmxisf-exports-darwin.txt"
  )
  set_property(
    TARGET mmxisf APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/mmxisf-exports-darwin.txt"
  )
endif()

if(MMXISF_BUILD_VIEWER)
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
  add_executable(mmxisf_public_api_contract_test
    tests/public_api_contract.cpp)
  target_link_libraries(mmxisf_public_api_contract_test
    PRIVATE mmxisf::mmxisf)
  add_test(NAME mmxisf.public-api-contract
    COMMAND mmxisf_public_api_contract_test)
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
  if(MMXISF_BUILD_VIEWER)
    add_executable(mmxisf_viewer_preview_test tests/viewer_preview_test.cpp)
    target_link_libraries(
      mmxisf_viewer_preview_test PRIVATE mmxisf_viewer_preview
    )
    add_test(NAME mmxisf.viewer-preview COMMAND mmxisf_viewer_preview_test)
  endif()
  add_test(
    NAME mmxisf.planning
    COMMAND
      ${CMAKE_COMMAND}
      -DMMXISF_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ValidatePlanning.cmake
  )
  add_test(
    NAME mmxisf.support-profile
    COMMAND
      ${CMAKE_COMMAND}
      -DMMXISF_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ValidateSupportProfile.cmake
  )
  add_test(
    NAME mmxisf.support-profile-contract
    COMMAND
      ${CMAKE_COMMAND}
      -DMMXISF_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -DMMXISF_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ValidateSupportProfileTests.cmake
  )
  add_test(
    NAME mmxisf.readiness
    COMMAND
      ${CMAKE_COMMAND}
      -DMMXISF_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ValidateReadiness.cmake
  )
  add_test(
    NAME mmxisf.readiness-contract
    COMMAND
      ${CMAKE_COMMAND}
      -DMMXISF_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -DMMXISF_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ValidateReadinessTests.cmake
  )
  add_test(
    NAME mmxisf.source-candidate-contract
    COMMAND
      ${CMAKE_COMMAND}
      -DMMXISF_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -DMMXISF_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ValidateSourceCandidateTests.cmake
  )
  add_test(
    NAME mmxisf.public-api-baseline
    COMMAND
      ${CMAKE_COMMAND}
      -DMMXISF_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ValidatePublicApiBaseline.cmake
  )
  add_test(
    NAME mmxisf.public-api-baseline-contract
    COMMAND
      ${CMAKE_COMMAND}
      -DMMXISF_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -DMMXISF_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ValidatePublicApiBaselineTests.cmake
  )
  add_test(
    NAME mmxisf.sbom
    COMMAND
      ${CMAKE_COMMAND}
      -DMMXISF_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -DMMXISF_PROJECT_VERSION=${PROJECT_VERSION}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ValidateSbom.cmake
  )
  add_test(
    NAME mmxisf.binary-sbom
    COMMAND
      ${CMAKE_COMMAND}
      -DMMXISF_BINARY_SBOM=${CMAKE_CURRENT_BINARY_DIR}/binary-dependencies.spdx.json
      -DMMXISF_PROJECT_VERSION=${PROJECT_VERSION}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ValidateBinarySbom.cmake
  )
  add_test(
    NAME mmxisf.package-version-contract
    COMMAND
      ${CMAKE_COMMAND}
      -DMMXISF_PACKAGE_VERSION_FILE=${CMAKE_CURRENT_BINARY_DIR}/mmxisfConfigVersion.cmake
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ValidatePackageVersion.cmake
  )
  if(BUILD_SHARED_LIBS AND (UNIX OR MINGW))
    add_test(
      NAME mmxisf.shared-export-surface
      COMMAND
        ${CMAKE_COMMAND}
        -DMMXISF_SHARED_LIBRARY=$<TARGET_FILE:mmxisf>
        -DMMXISF_NM=${CMAKE_NM}
        -DMMXISF_OBJDUMP=${CMAKE_OBJDUMP}
        -DMMXISF_PLATFORM=${CMAKE_SYSTEM_NAME}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ValidateSharedExports.cmake
    )
  endif()
  add_test(
    NAME mmxisf.production-dependency-baseline-contract
    COMMAND
      ${CMAKE_COMMAND}
      -DMMXISF_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ValidateProductionDependencyBaselineTests.cmake
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

if(MMXISF_BUILD_EXAMPLES)
  add_subdirectory(examples)
  if(MMXISF_BUILD_TESTS)
    add_test(
      NAME mmxisf.examples
      COMMAND
        ${CMAKE_COMMAND}
        -DMMXISF_READ_EXAMPLE=$<TARGET_FILE:mmxisf_example_read>
        -DMMXISF_WRITE_EXAMPLE=$<TARGET_FILE:mmxisf_example_write>
        -DMMXISF_EXAMPLE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mmxisf-example.xisf
        "-DMMXISF_TEST_EMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/RunExamples.cmake
    )
  endif()
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

if(MMXISF_INSTALL OR MMXISF_BUILD_TESTS)
  configure_package_config_file(
    cmake/mmxisfConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/mmxisfConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/mmxisf"
    PATH_VARS
      MMXISF_INSTALL_SBOM_DIR
      MMXISF_INSTALL_EXAMPLES_DIR
      MMXISF_INSTALL_CONFORMANCE_DIR
  )

  if(PROJECT_VERSION_MAJOR EQUAL 0)
    set(MMXISF_PACKAGE_VERSION_COMPATIBILITY SameMinorVersion)
  else()
    set(MMXISF_PACKAGE_VERSION_COMPATIBILITY SameMajorVersion)
  endif()

  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/mmxisfConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY ${MMXISF_PACKAGE_VERSION_COMPATIBILITY}
  )
endif()

if(MMXISF_INSTALL)
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
    DIRECTORY examples/
    DESTINATION "${MMXISF_INSTALL_EXAMPLES_DIR}"
    FILES_MATCHING
      PATTERN "CMakeLists.txt"
      PATTERN "*.cpp"
  )
  install(
    FILES LICENSE NOTICE SECURITY.md CONTRIBUTING.md THIRD_PARTY_NOTICES.md
    DESTINATION "${CMAKE_INSTALL_DATADIR}/mmxisf"
  )
  install(
    FILES
      sbom/source-dependencies.spdx.json
      "${CMAKE_CURRENT_BINARY_DIR}/binary-dependencies.spdx.json"
    DESTINATION "${MMXISF_INSTALL_SBOM_DIR}"
  )
  install(
    FILES
      docs/support-profile-0.1.0.json
      docs/public-api-baseline-0.1.0.json
      docs/conformance/xisf-1.0-matrix.json
      docs/specification-baseline.json
    DESTINATION "${MMXISF_INSTALL_CONFORMANCE_DIR}"
  )
  if(MMXISF_BUILD_DOCS)
    install(
      DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/api/html/"
      DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/mmxisf/api"
    )
  endif()
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
endif()
