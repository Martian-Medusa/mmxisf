# SPDX-License-Identifier: Apache-2.0

function(mmxisf_numeric_define header macro output)
  if(NOT EXISTS "${header}")
    message(FATAL_ERROR "Dependency version header is missing: ${header}")
  endif()
  file(
    STRINGS "${header}" matches
    REGEX "^[ \t]*#[ \t]*define[ \t]+${macro}[ \t]+[0-9]+"
  )
  list(LENGTH matches match_count)
  if(NOT match_count EQUAL 1)
    message(FATAL_ERROR
      "Expected one numeric ${macro} definition in ${header}, found ${match_count}"
    )
  endif()
  list(GET matches 0 definition)
  string(
    REGEX REPLACE
    "^[ \t]*#[ \t]*define[ \t]+${macro}[ \t]+([0-9]+).*$"
    "\\1" value "${definition}"
  )
  set(${output} "${value}" PARENT_SCOPE)
endfunction()

function(mmxisf_quoted_define header macro output)
  if(NOT EXISTS "${header}")
    message(FATAL_ERROR "Dependency version header is missing: ${header}")
  endif()
  file(
    STRINGS "${header}" matches
    REGEX "^[ \t]*#[ \t]*define[ \t]+${macro}[ \t]+\"[^\"]+\""
  )
  list(LENGTH matches match_count)
  if(NOT match_count EQUAL 1)
    message(FATAL_ERROR
      "Expected one quoted ${macro} definition in ${header}, found ${match_count}"
    )
  endif()
  list(GET matches 0 definition)
  string(
    REGEX REPLACE
    "^[ \t]*#[ \t]*define[ \t]+${macro}[ \t]+\"([^\"]+)\".*$"
    "\\1" value "${definition}"
  )
  set(${output} "${value}" PARENT_SCOPE)
endfunction()

mmxisf_numeric_define(
  "${EXPAT_INCLUDE_DIR}/expat.h" XML_MAJOR_VERSION MMXISF_EXPAT_MAJOR
)
mmxisf_numeric_define(
  "${EXPAT_INCLUDE_DIR}/expat.h" XML_MINOR_VERSION MMXISF_EXPAT_MINOR
)
mmxisf_numeric_define(
  "${EXPAT_INCLUDE_DIR}/expat.h" XML_MICRO_VERSION MMXISF_EXPAT_MICRO
)
set(MMXISF_EXPAT_VERSION
  "${MMXISF_EXPAT_MAJOR}.${MMXISF_EXPAT_MINOR}.${MMXISF_EXPAT_MICRO}"
)

mmxisf_quoted_define(
  "${ZLIB_INCLUDE_DIR}/zlib.h" ZLIB_VERSION MMXISF_ZLIB_VERSION
)

mmxisf_numeric_define(
  "${LZ4_INCLUDE_DIR}/lz4.h" LZ4_VERSION_MAJOR MMXISF_LZ4_MAJOR
)
mmxisf_numeric_define(
  "${LZ4_INCLUDE_DIR}/lz4.h" LZ4_VERSION_MINOR MMXISF_LZ4_MINOR
)
mmxisf_numeric_define(
  "${LZ4_INCLUDE_DIR}/lz4.h" LZ4_VERSION_RELEASE MMXISF_LZ4_RELEASE
)
set(MMXISF_LZ4_VERSION
  "${MMXISF_LZ4_MAJOR}.${MMXISF_LZ4_MINOR}.${MMXISF_LZ4_RELEASE}"
)

mmxisf_numeric_define(
  "${ZSTD_INCLUDE_DIR}/zstd.h" ZSTD_VERSION_MAJOR MMXISF_ZSTD_MAJOR
)
mmxisf_numeric_define(
  "${ZSTD_INCLUDE_DIR}/zstd.h" ZSTD_VERSION_MINOR MMXISF_ZSTD_MINOR
)
mmxisf_numeric_define(
  "${ZSTD_INCLUDE_DIR}/zstd.h" ZSTD_VERSION_RELEASE MMXISF_ZSTD_RELEASE
)
set(MMXISF_ZSTD_VERSION
  "${MMXISF_ZSTD_MAJOR}.${MMXISF_ZSTD_MINOR}.${MMXISF_ZSTD_RELEASE}"
)

mmxisf_quoted_define(
  "${OPENSSL_INCLUDE_DIR}/openssl/opensslv.h"
  OPENSSL_VERSION_STR
  MMXISF_OPENSSL_VERSION
)

foreach(version
    MMXISF_EXPAT_VERSION
    MMXISF_ZLIB_VERSION
    MMXISF_LZ4_VERSION
    MMXISF_ZSTD_VERSION
    MMXISF_OPENSSL_VERSION)
  if(NOT ${version} MATCHES "^[0-9]+\\.[0-9]+")
    message(FATAL_ERROR "Invalid resolved dependency version: ${version}")
  endif()
endforeach()
