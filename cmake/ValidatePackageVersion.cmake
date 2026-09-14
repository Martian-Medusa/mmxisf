# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED MMXISF_PACKAGE_VERSION_FILE OR
   NOT EXISTS "${MMXISF_PACKAGE_VERSION_FILE}")
  message(FATAL_ERROR "MMXISF_PACKAGE_VERSION_FILE is required")
endif()

function(mmxisf_expect_package_version requested expected_compatible
         expected_exact)
  unset(PACKAGE_VERSION)
  unset(PACKAGE_VERSION_COMPATIBLE)
  unset(PACKAGE_VERSION_EXACT)
  unset(PACKAGE_VERSION_UNSUITABLE)
  unset(PACKAGE_FIND_VERSION_RANGE)

  set(PACKAGE_FIND_VERSION "${requested}")
  string(REPLACE "." ";" _version_parts "${requested}")
  list(LENGTH _version_parts PACKAGE_FIND_VERSION_COUNT)
  list(GET _version_parts 0 PACKAGE_FIND_VERSION_MAJOR)
  if(PACKAGE_FIND_VERSION_COUNT GREATER 1)
    list(GET _version_parts 1 PACKAGE_FIND_VERSION_MINOR)
  else()
    set(PACKAGE_FIND_VERSION_MINOR 0)
  endif()
  if(PACKAGE_FIND_VERSION_COUNT GREATER 2)
    list(GET _version_parts 2 PACKAGE_FIND_VERSION_PATCH)
  else()
    set(PACKAGE_FIND_VERSION_PATCH 0)
  endif()
  set(PACKAGE_FIND_VERSION_TWEAK 0)

  include("${MMXISF_PACKAGE_VERSION_FILE}")

  if(PACKAGE_VERSION_COMPATIBLE)
    set(_compatible TRUE)
  else()
    set(_compatible FALSE)
  endif()
  if(PACKAGE_VERSION_EXACT)
    set(_exact TRUE)
  else()
    set(_exact FALSE)
  endif()

  if(NOT "${_compatible}" STREQUAL "${expected_compatible}" OR
     NOT "${_exact}" STREQUAL "${expected_exact}")
    message(FATAL_ERROR
      "Package version ${PACKAGE_VERSION} answered compatible=${_compatible}, "
      "exact=${_exact} for request ${requested}; expected "
      "compatible=${expected_compatible}, exact=${expected_exact}")
  endif()
endfunction()

mmxisf_expect_package_version("0.1.0" TRUE TRUE)
mmxisf_expect_package_version("0.1" TRUE FALSE)
mmxisf_expect_package_version("0.0.9" FALSE FALSE)
mmxisf_expect_package_version("0.2.0" FALSE FALSE)
mmxisf_expect_package_version("1.0.0" FALSE FALSE)

message(STATUS "Validated pre-1.0 package compatibility contract")
