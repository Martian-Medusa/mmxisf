add_executable(mmxisf_viewer MACOSX_BUNDLE Viewer.mm)
set(
  MMXISF_VIEWER_RESOURCE_FILES
  "${PROJECT_SOURCE_DIR}/LICENSE"
  "${PROJECT_SOURCE_DIR}/NOTICE"
  "${PROJECT_SOURCE_DIR}/SECURITY.md"
  "${PROJECT_SOURCE_DIR}/CONTRIBUTING.md"
  "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
  "${PROJECT_BINARY_DIR}/binary-dependencies.spdx.json"
)
target_sources(mmxisf_viewer PRIVATE ${MMXISF_VIEWER_RESOURCE_FILES})
set_source_files_properties(
  ${MMXISF_VIEWER_RESOURCE_FILES}
  PROPERTIES MACOSX_PACKAGE_LOCATION Resources
)
set_source_files_properties(
  "${PROJECT_BINARY_DIR}/binary-dependencies.spdx.json"
  PROPERTIES GENERATED TRUE
)
target_compile_features(mmxisf_viewer PRIVATE cxx_std_20)
target_compile_options(mmxisf_viewer PRIVATE -fobjc-arc)
target_link_libraries(
  mmxisf_viewer
  PRIVATE
    mmxisf_viewer_preview
    "-framework AppKit"
    "-framework QuartzCore"
    "-framework UniformTypeIdentifiers"
)

if(EXPAT_LIBRARY MATCHES "\\.dylib$")
  get_filename_component(MMXISF_EXPAT_REAL_PATH "${EXPAT_LIBRARY}" REALPATH)
  execute_process(
    COMMAND otool -D "${MMXISF_EXPAT_REAL_PATH}"
    OUTPUT_VARIABLE MMXISF_EXPAT_ID_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE MMXISF_EXPAT_ID_RESULT
  )
  if(NOT MMXISF_EXPAT_ID_RESULT EQUAL 0)
    message(FATAL_ERROR "Unable to inspect the Expat install name")
  endif()
  string(REPLACE "\n" ";" MMXISF_EXPAT_ID_LINES "${MMXISF_EXPAT_ID_OUTPUT}")
  list(GET MMXISF_EXPAT_ID_LINES -1 MMXISF_EXPAT_INSTALL_NAME)
  set_target_properties(
    mmxisf_viewer
    PROPERTIES BUILD_RPATH "@executable_path/../Frameworks"
  )
  add_custom_command(
    TARGET mmxisf_viewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:mmxisf_viewer>/Contents/Frameworks"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MMXISF_EXPAT_REAL_PATH}"
            "$<TARGET_BUNDLE_DIR:mmxisf_viewer>/Contents/Frameworks/libexpat.1.dylib"
    COMMAND install_name_tool -id "@rpath/libexpat.1.dylib"
            "$<TARGET_BUNDLE_DIR:mmxisf_viewer>/Contents/Frameworks/libexpat.1.dylib"
    COMMAND install_name_tool -change "${MMXISF_EXPAT_INSTALL_NAME}"
            "@rpath/libexpat.1.dylib"
            "$<TARGET_FILE:mmxisf_viewer>"
    COMMAND /usr/bin/codesign --force --sign -
            "$<TARGET_BUNDLE_DIR:mmxisf_viewer>/Contents/Frameworks/libexpat.1.dylib"
    VERBATIM
  )
endif()

if(ZLIB_LIBRARY MATCHES "\\.dylib$")
  get_filename_component(MMXISF_ZLIB_REAL_PATH "${ZLIB_LIBRARY}" REALPATH)
  execute_process(
    COMMAND otool -D "${MMXISF_ZLIB_REAL_PATH}"
    OUTPUT_VARIABLE MMXISF_ZLIB_ID_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE MMXISF_ZLIB_ID_RESULT
  )
  if(NOT MMXISF_ZLIB_ID_RESULT EQUAL 0)
    message(FATAL_ERROR "Unable to inspect the zlib install name")
  endif()
  string(REPLACE "\n" ";" MMXISF_ZLIB_ID_LINES "${MMXISF_ZLIB_ID_OUTPUT}")
  list(GET MMXISF_ZLIB_ID_LINES -1 MMXISF_ZLIB_INSTALL_NAME)
  get_filename_component(MMXISF_ZLIB_BUNDLE_NAME
                         "${MMXISF_ZLIB_INSTALL_NAME}" NAME)
  set_target_properties(
    mmxisf_viewer
    PROPERTIES BUILD_RPATH "@executable_path/../Frameworks"
  )
  add_custom_command(
    TARGET mmxisf_viewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:mmxisf_viewer>/Contents/Frameworks"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MMXISF_ZLIB_REAL_PATH}"
            "$<TARGET_BUNDLE_DIR:mmxisf_viewer>/Contents/Frameworks/${MMXISF_ZLIB_BUNDLE_NAME}"
    COMMAND install_name_tool -id "@rpath/${MMXISF_ZLIB_BUNDLE_NAME}"
            "$<TARGET_BUNDLE_DIR:mmxisf_viewer>/Contents/Frameworks/${MMXISF_ZLIB_BUNDLE_NAME}"
    COMMAND install_name_tool -change "${MMXISF_ZLIB_INSTALL_NAME}"
            "@rpath/${MMXISF_ZLIB_BUNDLE_NAME}"
            "$<TARGET_FILE:mmxisf_viewer>"
    COMMAND /usr/bin/codesign --force --sign -
            "$<TARGET_BUNDLE_DIR:mmxisf_viewer>/Contents/Frameworks/${MMXISF_ZLIB_BUNDLE_NAME}"
    VERBATIM
  )
endif()

if(LZ4_LIBRARY MATCHES "\\.dylib$")
  get_filename_component(MMXISF_LZ4_REAL_PATH "${LZ4_LIBRARY}" REALPATH)
  execute_process(
    COMMAND otool -D "${MMXISF_LZ4_REAL_PATH}"
    OUTPUT_VARIABLE MMXISF_LZ4_ID_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE MMXISF_LZ4_ID_RESULT
  )
  if(NOT MMXISF_LZ4_ID_RESULT EQUAL 0)
    message(FATAL_ERROR "Unable to inspect the LZ4 install name")
  endif()
  string(REPLACE "\n" ";" MMXISF_LZ4_ID_LINES "${MMXISF_LZ4_ID_OUTPUT}")
  list(GET MMXISF_LZ4_ID_LINES -1 MMXISF_LZ4_INSTALL_NAME)
  get_filename_component(MMXISF_LZ4_BUNDLE_NAME
                         "${MMXISF_LZ4_INSTALL_NAME}" NAME)
  set_target_properties(
    mmxisf_viewer
    PROPERTIES BUILD_RPATH "@executable_path/../Frameworks"
  )
  add_custom_command(
    TARGET mmxisf_viewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:mmxisf_viewer>/Contents/Frameworks"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MMXISF_LZ4_REAL_PATH}"
            "$<TARGET_BUNDLE_DIR:mmxisf_viewer>/Contents/Frameworks/${MMXISF_LZ4_BUNDLE_NAME}"
    COMMAND install_name_tool -id "@rpath/${MMXISF_LZ4_BUNDLE_NAME}"
            "$<TARGET_BUNDLE_DIR:mmxisf_viewer>/Contents/Frameworks/${MMXISF_LZ4_BUNDLE_NAME}"
    COMMAND install_name_tool -change "${MMXISF_LZ4_INSTALL_NAME}"
            "@rpath/${MMXISF_LZ4_BUNDLE_NAME}"
            "$<TARGET_FILE:mmxisf_viewer>"
    COMMAND /usr/bin/codesign --force --sign -
            "$<TARGET_BUNDLE_DIR:mmxisf_viewer>/Contents/Frameworks/${MMXISF_LZ4_BUNDLE_NAME}"
    VERBATIM
  )
endif()

if(ZSTD_LIBRARY MATCHES "\\.dylib$")
  get_filename_component(MMXISF_ZSTD_REAL_PATH "${ZSTD_LIBRARY}" REALPATH)
  execute_process(
    COMMAND otool -D "${MMXISF_ZSTD_REAL_PATH}"
    OUTPUT_VARIABLE MMXISF_ZSTD_ID_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE MMXISF_ZSTD_ID_RESULT
  )
  if(NOT MMXISF_ZSTD_ID_RESULT EQUAL 0)
    message(FATAL_ERROR "Unable to inspect the Zstandard install name")
  endif()
  string(REPLACE "\n" ";" MMXISF_ZSTD_ID_LINES "${MMXISF_ZSTD_ID_OUTPUT}")
  list(GET MMXISF_ZSTD_ID_LINES -1 MMXISF_ZSTD_INSTALL_NAME)
  get_filename_component(MMXISF_ZSTD_BUNDLE_NAME
                         "${MMXISF_ZSTD_INSTALL_NAME}" NAME)
  set_target_properties(
    mmxisf_viewer
    PROPERTIES BUILD_RPATH "@executable_path/../Frameworks"
  )
  add_custom_command(
    TARGET mmxisf_viewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:mmxisf_viewer>/Contents/Frameworks"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MMXISF_ZSTD_REAL_PATH}"
            "$<TARGET_BUNDLE_DIR:mmxisf_viewer>/Contents/Frameworks/${MMXISF_ZSTD_BUNDLE_NAME}"
    COMMAND install_name_tool -id "@rpath/${MMXISF_ZSTD_BUNDLE_NAME}"
            "$<TARGET_BUNDLE_DIR:mmxisf_viewer>/Contents/Frameworks/${MMXISF_ZSTD_BUNDLE_NAME}"
    COMMAND install_name_tool -change "${MMXISF_ZSTD_INSTALL_NAME}"
            "@rpath/${MMXISF_ZSTD_BUNDLE_NAME}"
            "$<TARGET_FILE:mmxisf_viewer>"
    COMMAND /usr/bin/codesign --force --sign -
            "$<TARGET_BUNDLE_DIR:mmxisf_viewer>/Contents/Frameworks/${MMXISF_ZSTD_BUNDLE_NAME}"
    VERBATIM
  )
endif()

if(OPENSSL_CRYPTO_LIBRARY MATCHES "\\.dylib$")
  get_filename_component(MMXISF_CRYPTO_REAL_PATH
                         "${OPENSSL_CRYPTO_LIBRARY}" REALPATH)
  execute_process(
    COMMAND otool -D "${MMXISF_CRYPTO_REAL_PATH}"
    OUTPUT_VARIABLE MMXISF_CRYPTO_ID_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE MMXISF_CRYPTO_ID_RESULT
  )
  if(NOT MMXISF_CRYPTO_ID_RESULT EQUAL 0)
    message(FATAL_ERROR "Unable to inspect the OpenSSL Crypto install name")
  endif()
  string(REPLACE "\n" ";" MMXISF_CRYPTO_ID_LINES
                 "${MMXISF_CRYPTO_ID_OUTPUT}")
  list(GET MMXISF_CRYPTO_ID_LINES -1 MMXISF_CRYPTO_INSTALL_NAME)
  get_filename_component(MMXISF_CRYPTO_BUNDLE_NAME
                         "${MMXISF_CRYPTO_INSTALL_NAME}" NAME)
  set_target_properties(
    mmxisf_viewer
    PROPERTIES BUILD_RPATH "@executable_path/../Frameworks"
  )
  add_custom_command(
    TARGET mmxisf_viewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:mmxisf_viewer>/Contents/Frameworks"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MMXISF_CRYPTO_REAL_PATH}"
            "$<TARGET_BUNDLE_DIR:mmxisf_viewer>/Contents/Frameworks/${MMXISF_CRYPTO_BUNDLE_NAME}"
    COMMAND install_name_tool -id "@rpath/${MMXISF_CRYPTO_BUNDLE_NAME}"
            "$<TARGET_BUNDLE_DIR:mmxisf_viewer>/Contents/Frameworks/${MMXISF_CRYPTO_BUNDLE_NAME}"
    COMMAND install_name_tool -change "${MMXISF_CRYPTO_INSTALL_NAME}"
            "@rpath/${MMXISF_CRYPTO_BUNDLE_NAME}"
            "$<TARGET_FILE:mmxisf_viewer>"
    COMMAND /usr/bin/codesign --force --sign -
            "$<TARGET_BUNDLE_DIR:mmxisf_viewer>/Contents/Frameworks/${MMXISF_CRYPTO_BUNDLE_NAME}"
    VERBATIM
  )
endif()

if(OPENSSL_CRYPTO_LIBRARY MATCHES "\\.dylib$" AND
   ZLIB_LIBRARY MATCHES "\\.dylib$")
  add_custom_command(
    TARGET mmxisf_viewer POST_BUILD
    COMMAND install_name_tool -change "${MMXISF_ZLIB_INSTALL_NAME}"
            "@rpath/${MMXISF_ZLIB_BUNDLE_NAME}"
            "$<TARGET_BUNDLE_DIR:mmxisf_viewer>/Contents/Frameworks/${MMXISF_CRYPTO_BUNDLE_NAME}"
    COMMAND /usr/bin/codesign --force --sign -
            "$<TARGET_BUNDLE_DIR:mmxisf_viewer>/Contents/Frameworks/${MMXISF_CRYPTO_BUNDLE_NAME}"
    VERBATIM
  )
endif()

# Sign the complete bundle once, after every optional copied dylib has been
# rewritten and signed. This is also required for all-static dependency builds:
# signing only the Mach-O executable leaves Resources outside the code seal.
add_custom_command(
  TARGET mmxisf_viewer POST_BUILD
  COMMAND /usr/bin/codesign --force --deep --sign -
          "$<TARGET_BUNDLE_DIR:mmxisf_viewer>"
  VERBATIM
)

set_target_properties(
  mmxisf_viewer
  PROPERTIES
    OUTPUT_NAME "mmXISF Viewer PoC"
    RUNTIME_OUTPUT_DIRECTORY "${MMXISF_VIEWER_OUTPUT_DIR}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "mmXISF Viewer PoC"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.martianmedusa.mmxisf.viewer-poc"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

if(BUILD_TESTING AND MMXISF_BUILD_TESTS)
  add_test(
    NAME mmxisf.viewer-bundle
    COMMAND
      ${CMAKE_COMMAND}
      -DMMXISF_VIEWER_BUNDLE=$<TARGET_BUNDLE_DIR:mmxisf_viewer>
      -DMMXISF_PROJECT_VERSION=${PROJECT_VERSION}
      -P ${PROJECT_SOURCE_DIR}/cmake/ValidateViewerBundle.cmake
  )
endif()
