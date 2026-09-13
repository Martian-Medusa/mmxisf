add_executable(mmxisf_viewer MACOSX_BUNDLE Viewer.mm)
target_sources(mmxisf_viewer PRIVATE "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md")
set_source_files_properties(
  "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
  PROPERTIES MACOSX_PACKAGE_LOCATION Resources
)
target_compile_features(mmxisf_viewer PRIVATE cxx_std_20)
target_compile_options(mmxisf_viewer PRIVATE -fobjc-arc)
target_link_libraries(
  mmxisf_viewer
  PRIVATE
    mmxisf::mmxisf
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
    COMMAND /usr/bin/codesign --force --sign -
            "$<TARGET_BUNDLE_DIR:mmxisf_viewer>"
    VERBATIM
  )
endif()

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
