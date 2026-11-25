# Compile suites in their original dependency contexts, then link one runner.
# CTest still starts a fresh process for each registered command.
add_executable(fugue_tests ${PROJECT_SOURCE_DIR}/tests/runner.cpp)
target_include_directories(fugue_tests PRIVATE ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(fugue_tests PRIVATE Qt6::Core Qt6::Test)

# Entries start as portable and fast: headless checks that take a few seconds each, the inner-loop selection
# (`ctest -L fast`). An entry that needs a GPU device, a window, a device or WebEngine replaces these labels.
function(fugue_add_test name)
  add_test(NAME ${name} COMMAND ${ARGN})
  set_tests_properties(${name} PROPERTIES LABELS "portable;fast" TIMEOUT 120
    FAIL_REGULAR_EXPRESSION "SKIP[ \t]*:"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FUGUE_AUDIO=none;QTWEBENGINE_CHROMIUM_FLAGS=--no-sandbox --disable-gpu")
endfunction()

# fugue_add_sharded_test(<name> <count> <command...>): <count> entries, <name>_1of<count> and so on, that
# together run the suite once, so `ctest -j` spreads a long CPU-bound suite over cores. The runner chooses each
# entry's test functions (--shard, tests/runner.cpp); only suites using a FUGUE_TEST_*MAIN entry accept it.
# Their names are returned in <name>_SHARDS.
function(fugue_add_sharded_test name count)
  set(shards)
  foreach(part RANGE 1 ${count})
    fugue_add_test(${name}_${part}of${count} ${ARGN} --shard=${part}/${count})
    list(APPEND shards ${name}_${part}of${count})
  endforeach()
  set(${name}_SHARDS ${shards} PARENT_SCOPE)
endfunction()

function(fugue_test_entry name entry)
  get_property(names GLOBAL PROPERTY FUGUE_TEST_NAMES)
  if(name IN_LIST names)
    message(FATAL_ERROR "Duplicate test suite: ${name}")
  endif()
  set_property(GLOBAL APPEND PROPERTY FUGUE_TEST_NAMES "${name}")
  set_property(GLOBAL APPEND_STRING PROPERTY FUGUE_TEST_ENTRIES
    "FUGUE_TEST_SUITE(\"${name}\", ${entry})\n")
endfunction()

function(fugue_test_suite target source)
  add_library(${target} OBJECT ${source})
  target_compile_definitions(${target} PRIVATE FUGUE_TEST_ENTRY=fugue_run_${target})
  string(REGEX REPLACE "_test$" "" name "${target}")
  fugue_test_entry("${name}" fugue_run_${target})
  set_property(GLOBAL APPEND PROPERTY FUGUE_TEST_TARGETS "${target}")
endfunction()

function(fugue_finish_tests)
  get_property(targets GLOBAL PROPERTY FUGUE_TEST_TARGETS)
  foreach(target IN LISTS targets)
    # OBJECT dependencies must also reach the executable directly: their
    # registration objects are not propagated through another OBJECT library.
    get_target_property(libraries ${target} LINK_LIBRARIES)
    target_link_libraries(fugue_tests PRIVATE ${target})
    foreach(library IN LISTS libraries)
      if(TARGET ${library})
        get_target_property(type ${library} TYPE)
        if(type STREQUAL "OBJECT_LIBRARY")
          target_link_libraries(fugue_tests PRIVATE ${library})
        endif()
      endif()
    endforeach()
  endforeach()
  get_property(entries GLOBAL PROPERTY FUGUE_TEST_ENTRIES)
  file(CONFIGURE OUTPUT ${PROJECT_BINARY_DIR}/fugue_test_suites.inc
    CONTENT "${entries}" @ONLY)
endfunction()
