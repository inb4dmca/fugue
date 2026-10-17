# canon_json is a library (OBJECT, globbed).
fugue_module(fugue_canon DIR ${CMAKE_CURRENT_SOURCE_DIR}/canon_json)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/private.cmake)
  include(${CMAKE_CURRENT_SOURCE_DIR}/private.cmake)
endif()
