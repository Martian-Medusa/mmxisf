if(NOT DEFINED MMXISF_SOURCE_DIR OR NOT DEFINED MMXISF_PROJECT_VERSION)
  message(FATAL_ERROR "SBOM validation requires source dir and project version")
endif()

set(sbom_path "${MMXISF_SOURCE_DIR}/sbom/source-dependencies.spdx.json")
file(READ "${sbom_path}" sbom)

string(JSON spdx_version GET "${sbom}" spdxVersion)
if(NOT spdx_version STREQUAL "SPDX-2.3")
  message(FATAL_ERROR "Unexpected SPDX version: ${spdx_version}")
endif()

string(JSON package_count LENGTH "${sbom}" packages)
if(NOT package_count EQUAL 6)
  message(FATAL_ERROR "SBOM must contain mmxisf and five direct dependencies")
endif()

string(JSON package_name GET "${sbom}" packages 0 name)
string(JSON package_version GET "${sbom}" packages 0 versionInfo)
if(NOT package_name STREQUAL "mmxisf" OR
   NOT package_version STREQUAL "${MMXISF_PROJECT_VERSION}")
  message(FATAL_ERROR "SBOM project identity/version does not match CMake")
endif()

foreach(index RANGE 1 5)
  string(JSON license GET "${sbom}" packages ${index} licenseDeclared)
  if(license STREQUAL "NOASSERTION")
    message(FATAL_ERROR "Direct dependency ${index} lacks a declared license")
  endif()
endforeach()
