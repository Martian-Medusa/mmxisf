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

option(MMXISF_BUILD_TESTS "Build mmxisf tests" ${BUILD_TESTING})
option(MMXISF_BUILD_TOOLS "Build mmxisf command-line tools" ON)
option(MMXISF_BUILD_VIEWER "Build the macOS PoC viewer" OFF)

find_package(EXPAT 2.2 REQUIRED)

add_library(mmxisf src/reader.cpp src/version.cpp)
add_library(mmxisf::mmxisf ALIAS mmxisf)

target_compile_features(mmxisf PUBLIC cxx_std_20)
target_include_directories(
  mmxisf
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(mmxisf PRIVATE EXPAT::EXPAT)

set_target_properties(
  mmxisf
  PROPERTIES
    CXX_EXTENSIONS OFF
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
  add_test(
    NAME mmxisf.planning
    COMMAND
      ${CMAKE_COMMAND}
      -DMMXISF_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ValidatePlanning.cmake
  )
endif()

if(MMXISF_BUILD_TOOLS)
  add_executable(mmxisf_inspect tools/mmxisf_inspect.cpp)
  set_target_properties(mmxisf_inspect PROPERTIES OUTPUT_NAME "mmxisf-inspect")
  target_link_libraries(mmxisf_inspect PRIVATE mmxisf::mmxisf)
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
  EXPORT mmxisfTargets
  FILE mmxisfTargets.cmake
  NAMESPACE mmxisf::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/mmxisf"
)
install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/mmxisfConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/mmxisfConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/mmxisf"
)
