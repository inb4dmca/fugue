fugue_module(fugue_audio)
# Tracker modules (MOD/S3M/XM/IT, audio/module.cpp) play through libopenmpt, found with pkg-config ("libopenmpt").
#   AUTO (default): used when found; otherwise modules are refused with a warning.
#   ON: required (the Flatpak build sets it), so modules cannot be missing silently.
#   OFF: never used.
set(FUGUE_OPENMPT AUTO CACHE STRING "tracker-module playback via libopenmpt: AUTO, ON or OFF")
set_property(CACHE FUGUE_OPENMPT PROPERTY STRINGS AUTO ON OFF)
string(TOUPPER "${FUGUE_OPENMPT}" _fugue_openmpt)
if(NOT _fugue_openmpt STREQUAL "OFF")
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(OPENMPT QUIET IMPORTED_TARGET libopenmpt)
  endif()
  if(OPENMPT_FOUND)
    message(STATUS "fugue-qt: libopenmpt ${OPENMPT_VERSION} - tracker modules play")
    target_link_libraries(fugue_audio PUBLIC PkgConfig::OPENMPT)
    target_compile_definitions(fugue_audio PRIVATE FUGUE_HAVE_OPENMPT=1)
  elseif(_fugue_openmpt STREQUAL "AUTO")
    message(WARNING "fugue-qt: libopenmpt not found (pkg-config libopenmpt) - tracker modules (MOD/S3M/XM/IT) will be "
                    "refused. Install libopenmpt-dev or point CMAKE_PREFIX_PATH at a libopenmpt prefix to play them.")
  else()
    message(FATAL_ERROR "fugue-qt: FUGUE_OPENMPT=${FUGUE_OPENMPT} but libopenmpt was not found (pkg-config libopenmpt)")
  endif()
endif()
