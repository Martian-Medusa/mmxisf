# SPDX-License-Identifier: Apache-2.0

# Dated 2026-09-14. This is a release-candidate floor, not a perpetual claim
# that later advisories cannot affect these versions. Refresh the audit and
# this file immediately before freezing every publication candidate.

function(mmxisf_require_dependency_version name actual minimum)
  if("${actual}" VERSION_LESS "${minimum}")
    message(FATAL_ERROR
      "${name} ${actual} is below the 2026-09-14 mmxisf production baseline ${minimum}"
    )
  endif()
  message(STATUS
    "Production dependency baseline: ${name} ${actual} >= ${minimum}"
  )
endfunction()

foreach(required_version
    MMXISF_EXPAT_VERSION
    MMXISF_ZLIB_VERSION
    MMXISF_LZ4_VERSION
    MMXISF_ZSTD_VERSION
    MMXISF_OPENSSL_VERSION)
  if(NOT DEFINED ${required_version} OR "${${required_version}}" STREQUAL "")
    message(FATAL_ERROR
      "Production dependency baseline requires ${required_version}"
    )
  endif()
endforeach()

mmxisf_require_dependency_version(
  "Expat" "${MMXISF_EXPAT_VERSION}" "2.8.2"
)
mmxisf_require_dependency_version(
  "zlib" "${MMXISF_ZLIB_VERSION}" "1.3.2"
)
mmxisf_require_dependency_version(
  "LZ4" "${MMXISF_LZ4_VERSION}" "1.10.0"
)
mmxisf_require_dependency_version(
  "Zstandard" "${MMXISF_ZSTD_VERSION}" "1.5.7"
)
mmxisf_require_dependency_version(
  "OpenSSL" "${MMXISF_OPENSSL_VERSION}" "3.5.8"
)
