# Tests
#
# Unit tests for utility functions with self-contained logic, built as small
# executables linking libncutil and run by ctest (via ./make.sh tests).

# Test macro add_unit_test
macro(add_unit_test testname)
  add_executable(${testname} ${testname}.cpp)

  # Headers
  target_include_directories(${testname} PRIVATE "../lib/common/src")
  target_include_directories(${testname} PRIVATE "../lib/ncutil/src")

  # Compiler flags
  set_target_properties(${testname} PROPERTIES COMPILE_FLAGS
                        "-Wall -Wextra -Wpedantic -Wshadow -Wpointer-arith \
                         -Wcast-qual -Wno-missing-braces -Wswitch-default \
                         -Wunreachable-code -Wundef -Wuninitialized \
                         -Wcast-align")

  # Linking
  target_link_libraries(${testname} PUBLIC ncutil)

  # Common
  target_common_config(${testname})

  # Test registration. The timeout ensures a hang in a tested function fails
  # the test run, rather than blocking it indefinitely.
  add_test(NAME ${testname} COMMAND ${testname})
  set_tests_properties(${testname} PROPERTIES TIMEOUT 60)
endmacro()

# Unit tests
add_unit_test(test001)
add_unit_test(test002)
add_unit_test(test003)
target_sources(test003 PRIVATE ../src/uiactivity.cpp)
target_include_directories(test003 PRIVATE ../src)

add_unit_test(test004)
target_sources(test004 PRIVATE ../src/uidrafts.cpp)
target_include_directories(test004 PRIVATE ../src)

# The UI test uses fake accounts and ncurses' off-screen terminal. It reuses
# nchat's built objects so its send-routing assertions exercise the real model.
if(CMAKE_GENERATOR STREQUAL "Unix Makefiles")
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME test_ui_panes COMMAND ${Python3_EXECUTABLE}
             ${CMAKE_CURRENT_SOURCE_DIR}/run_ui_regression.py ${CMAKE_BINARY_DIR})
    set_tests_properties(test_ui_panes PROPERTIES TIMEOUT 60)
    find_program(CHAFA_EXECUTABLE chafa)
    if(CHAFA_EXECUTABLE)
      add_test(NAME test_image_preview COMMAND ${Python3_EXECUTABLE}
               ${CMAKE_CURRENT_SOURCE_DIR}/run_ui_regression.py ${CMAKE_BINARY_DIR} test_image_preview.cpp)
      set_tests_properties(test_image_preview PROPERTIES TIMEOUT 60)
      add_test(NAME test_preferences_preview COMMAND ${Python3_EXECUTABLE}
               ${CMAKE_CURRENT_SOURCE_DIR}/run_ui_regression.py ${CMAKE_BINARY_DIR} test_preferences_preview.cpp)
      set_tests_properties(test_preferences_preview PROPERTIES TIMEOUT 60)
      if(UNIX AND NOT APPLE)
        add_test(NAME test_pixel_graphics COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/run_ui_regression.py ${CMAKE_BINARY_DIR} test_pixel_graphics.cpp)
        set_tests_properties(test_pixel_graphics PROPERTIES TIMEOUT 60)
      endif()
    endif()
  endif()
endif()

# Stdio integration against real Dummy profiles, no accounts or terminal needed.
if(HAS_DUMMY)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME test_serve COMMAND ${Python3_EXECUTABLE}
             ${CMAKE_CURRENT_SOURCE_DIR}/run_serve_regression.py $<TARGET_FILE:nchat>)
    set_tests_properties(test_serve PROPERTIES TIMEOUT 60)
  endif()
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_executable(serve_fixture serve_fixture.cpp ../src/serve.cpp)
  target_include_directories(serve_fixture PRIVATE ../src ../lib/common/src ../lib/ncutil/src
                             ../lib/ncutil/ext/cereal/include)
  target_link_libraries(serve_fixture PRIVATE ncutil pthread)
  target_common_config(serve_fixture)
  add_test(NAME test_serve_events COMMAND ${Python3_EXECUTABLE}
           ${CMAKE_CURRENT_SOURCE_DIR}/run_serve_events.py $<TARGET_FILE:serve_fixture>)
  set_tests_properties(test_serve_events PROPERTIES TIMEOUT 60)
endif()
