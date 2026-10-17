# app/: the fugue-qt window binary (session -> model -> render -> shell, plus the launcher and pager).
set(CMAKE_AUTOMOC ON)
file(GLOB app_srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(fugue-qt ${app_srcs})
target_link_libraries(fugue-qt PRIVATE fugue_shell fugue_audio fugue_net fugue_proto fugue_model fugue_render fugue_skin fugue_canon fugue_account fugue_pager fugue_pager_ui
                      Qt6::Widgets Qt6::WebEngineWidgets Qt6::WebChannel)
if(FUGUE_RELEASE)
  target_compile_definitions(fugue-qt PRIVATE FUGUE_RELEASE=1)   # no FUGUE_AUTO_* hooks, no pager --auto-join/--quit-after, PR_SET_DUMPABLE=0
endif()
add_test(NAME version COMMAND fugue-qt --version)
set_tests_properties(version PROPERTIES PASS_REGULAR_EXPRESSION "^fugue-qt ${PROJECT_VERSION}(-dev)?\n")
install(TARGETS fugue-qt RUNTIME DESTINATION bin)
