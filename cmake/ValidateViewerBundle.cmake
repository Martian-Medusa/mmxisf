cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED MMXISF_VIEWER_BUNDLE OR
   NOT DEFINED MMXISF_PROJECT_VERSION)
  message(FATAL_ERROR
    "MMXISF_VIEWER_BUNDLE and MMXISF_PROJECT_VERSION are required")
endif()
if(NOT IS_DIRECTORY "${MMXISF_VIEWER_BUNDLE}")
  message(FATAL_ERROR "Missing viewer bundle: ${MMXISF_VIEWER_BUNDLE}")
endif()

set(_resources "${MMXISF_VIEWER_BUNDLE}/Contents/Resources")
foreach(_resource IN ITEMS
    LICENSE
    NOTICE
    SECURITY.md
    CONTRIBUTING.md
    THIRD_PARTY_NOTICES.md
    binary-dependencies.spdx.json)
  if(NOT EXISTS "${_resources}/${_resource}")
    message(FATAL_ERROR "Viewer bundle is missing resource: ${_resource}")
  endif()
endforeach()

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    "-DMMXISF_BINARY_SBOM=${_resources}/binary-dependencies.spdx.json"
    "-DMMXISF_PROJECT_VERSION=${MMXISF_PROJECT_VERSION}"
    -P "${CMAKE_CURRENT_LIST_DIR}/ValidateBinarySbom.cmake"
  RESULT_VARIABLE _sbom_result
  OUTPUT_VARIABLE _sbom_output
  ERROR_VARIABLE _sbom_error
)
if(NOT _sbom_result EQUAL 0)
  message(FATAL_ERROR
    "Viewer binary SBOM validation failed: ${_sbom_output}${_sbom_error}")
endif()

execute_process(
  COMMAND /usr/bin/codesign --verify --deep --strict --verbose=2
          "${MMXISF_VIEWER_BUNDLE}"
  RESULT_VARIABLE _signature_result
  OUTPUT_VARIABLE _signature_output
  ERROR_VARIABLE _signature_error
)
if(NOT _signature_result EQUAL 0)
  message(FATAL_ERROR
    "Viewer code-signature validation failed: "
    "${_signature_output}${_signature_error}")
endif()

message(STATUS
  "Viewer distribution resources, binary SBOM, and deep signature: PASS")
