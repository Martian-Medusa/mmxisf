# SPDX-License-Identifier: Apache-2.0

include_guard(GLOBAL)

function(mmxisf_add_public_header_self_containment_test target_name header_directory)
  set(_mmxisf_public_headers
    byte_sink.hpp
    byte_source.hpp
    document.hpp
    error.hpp
    export.hpp
    reader.hpp
    result.hpp
    version.hpp
    writer.hpp
  )

  file(GLOB _mmxisf_discovered_public_headers
    RELATIVE "${header_directory}"
    "${header_directory}/*.hpp")
  list(SORT _mmxisf_discovered_public_headers)
  if(NOT _mmxisf_discovered_public_headers STREQUAL _mmxisf_public_headers)
    message(FATAL_ERROR
      "Public header self-containment manifest is stale: "
      "expected '${_mmxisf_public_headers}', discovered "
      "'${_mmxisf_discovered_public_headers}'")
  endif()

  set(_mmxisf_header_test_sources)
  foreach(_mmxisf_header IN LISTS _mmxisf_public_headers)
    string(REPLACE "." "_" _mmxisf_header_stem "${_mmxisf_header}")
    set(_mmxisf_header_test_source
      "${CMAKE_CURRENT_BINARY_DIR}/${target_name}-${_mmxisf_header_stem}.cpp")
    file(GENERATE
      OUTPUT "${_mmxisf_header_test_source}"
      CONTENT "#include <mmxisf/${_mmxisf_header}>\n")
    list(APPEND _mmxisf_header_test_sources
      "${_mmxisf_header_test_source}")
  endforeach()

  add_library(${target_name} OBJECT ${_mmxisf_header_test_sources})
  target_link_libraries(${target_name} PRIVATE mmxisf::mmxisf)
endfunction()
