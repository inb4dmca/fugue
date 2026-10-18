# app/: the fugue-qt window binary (session -> model -> render -> shell, plus the launcher and pager).
set(CMAKE_AUTOMOC ON)
file(GLOB app_srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(fugue-qt ${app_srcs})
target_link_libraries(fugue-qt PRIVATE fugue_shell fugue_audio fugue_net fugue_proto fugue_model fugue_render fugue_skin fugue_canon fugue_account fugue_pager fugue_pager_ui
                      Qt6::Widgets Qt6::WebEngineWidgets Qt6::WebChannel)
if(FUGUE_RELEASE)
  target_compile_definitions(fugue-qt PRIVATE FUGUE_RELEASE=1)   # no FUGUE_AUTO_* hooks, no pager --auto-join/--quit-after, disable core dumps
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/worldlink_dd_test.cpp)
  add_executable(worldlink_dd_test ${CMAKE_CURRENT_SOURCE_DIR}/tests/worldlink_dd_test.cpp)
  target_link_libraries(worldlink_dd_test PRIVATE Qt6::Core Qt6::Test)
  target_compile_definitions(worldlink_dd_test PRIVATE FUGUE_QT_BIN="$<TARGET_FILE:fugue-qt>")
  add_dependencies(worldlink_dd_test fugue-qt)
  add_test(NAME worldlink_dd COMMAND worldlink_dd_test)
endif()
add_test(NAME version COMMAND fugue-qt --version)
set_tests_properties(version PROPERTIES PASS_REGULAR_EXPRESSION "^fugue-qt ${PROJECT_VERSION}(-dev)?\n")
add_test(NAME pager_url_one_argument COMMAND fugue-qt pager --url "byond://127.0.0.1:1" -keytxt x)
set_tests_properties(pager_url_one_argument PROPERTIES PASS_REGULAR_EXPRESSION "pager: --url takes exactly one link")
if(APPLE)
  # Declare the SVG plugin's framework explicitly so macdeployqt can resolve it even with split Qt installations.
  find_package(Qt6 REQUIRED COMPONENTS Svg)
  # LSMinimumSystemVersion: the deployment target, else this Mac's version (what the compiler then targets at most).
  set(FUGUE_MACOS_MIN "${CMAKE_OSX_DEPLOYMENT_TARGET}")
  if(NOT FUGUE_MACOS_MIN)
    execute_process(COMMAND sw_vers -productVersion OUTPUT_VARIABLE FUGUE_MACOS_MIN OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  endif()
  target_link_libraries(fugue-qt PRIVATE Qt6::Svg)
  set_target_properties(fugue-qt PROPERTIES
    MACOSX_BUNDLE TRUE
    OUTPUT_NAME Fugue
    MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in")
  set(_icon "${CMAKE_CURRENT_BINARY_DIR}/Fugue.icns")
  file(GLOB _icon_sources "${PROJECT_SOURCE_DIR}/packaging/flatpak/icons/*.png")
  add_custom_command(OUTPUT "${_icon}"
    COMMAND ${CMAKE_COMMAND}
      "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}/packaging/flatpak/icons"
      "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
      -P "${PROJECT_SOURCE_DIR}/packaging/macos/Icon.cmake"
    DEPENDS ${_icon_sources} "${PROJECT_SOURCE_DIR}/packaging/macos/Icon.cmake"
    VERBATIM)
  target_sources(fugue-qt PRIVATE "${_icon}")
  set_source_files_properties("${_icon}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()
install(TARGETS fugue-qt BUNDLE DESTINATION . RUNTIME DESTINATION bin)
if(APPLE)
  # Use the deployment tool from the selected Qt, not another Qt installation on PATH.
  get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
  execute_process(COMMAND "${_qmake}" -query QT_INSTALL_LIBS
    OUTPUT_VARIABLE FUGUE_QT_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${_qmake}" -query QT_INSTALL_BINS
    OUTPUT_VARIABLE _qt_bins OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  find_program(FUGUE_MACDEPLOYQT macdeployqt HINTS "${_qt_bins}" NO_DEFAULT_PATH REQUIRED)
  configure_file("${PROJECT_SOURCE_DIR}/packaging/macos/Deploy.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Deploy.cmake" @ONLY)
  install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/Deploy.cmake")
endif()
