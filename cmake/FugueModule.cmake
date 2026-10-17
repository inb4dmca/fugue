# fugue_module(<target> [DIR <dir>])
#   OBJECT library of every *.cpp directly inside DIR (default: the calling CMakeLists' directory).
#   OBJECT, not STATIC: proto/ decoders register themselves from static initialisers, and a static
#   archive would silently drop any .o nothing references. Linking an OBJECT library with
#   target_link_libraries() carries its objects into the consumer (CMake >= 3.12).
function(fugue_module target)
  cmake_parse_arguments(FM "" "DIR" "" ${ARGN})
  if(NOT FM_DIR)
    set(FM_DIR ${CMAKE_CURRENT_SOURCE_DIR})
  endif()
  file(GLOB srcs CONFIGURE_DEPENDS ${FM_DIR}/*.cpp)
  add_library(${target} OBJECT ${srcs})
  target_include_directories(${target} PUBLIC ${PROJECT_SOURCE_DIR})
endfunction()
