set(CMAKE_AUTOMOC ON)
# Each tests/*_test.cpp becomes a suite in fugue_tests and a ctest named <stem>.
# New files are picked up on the next build (CONFIGURE_DEPENDS); no list to edit.
file(GLOB test_srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)
foreach(src ${test_srcs})
  get_filename_component(stem ${src} NAME_WE)
  if(WIN32 AND stem STREQUAL "platform_test")
    continue() # POSIX descriptor/pipe behavior; not available on Windows.
  endif()
  string(REGEX REPLACE "_test$" "" name ${stem})
  fugue_test_suite(${stem} ${src})
  target_link_libraries(${stem} PRIVATE fugue_audio fugue_net fugue_proto fugue_model fugue_render fugue_skin fugue_canon Qt6::Core Qt6::Test)
  target_compile_definitions(${stem} PRIVATE
    FUGUE_QT_ROOT="${PROJECT_SOURCE_DIR}"
    FUGUE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
  add_test(NAME ${name} COMMAND fugue_tests ${name} -o -,txt)
endforeach()
