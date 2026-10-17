# Project
cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(libigchat LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)

# Ccache
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

# Library
add_library(igchat ${LIBRARY_LINKING_TYPE}
  src/igchat.cpp
  src/igchat.h
)
install(TARGETS igchat DESTINATION ${CMAKE_INSTALL_LIBDIR})

# Common
target_common_config(igchat)

# Dependency libraries
add_subdirectory(go)
add_dependencies(igchat ref-cgoig)

# Platform specifics
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  add_compile_definitions(_XOPEN_SOURCE_EXTENDED)
  FIND_LIBRARY(CARBON_LIBRARY CoreFoundation)
  FIND_LIBRARY(CARBON_LIBRARY Security)
  target_link_libraries(igchat PUBLIC "-framework CoreFoundation" "-framework Security" resolv)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  # Go's cgo resolver may need libresolv, which a c-archive does not propagate
  target_link_libraries(igchat PUBLIC resolv)
endif()

# Headers
target_include_directories(igchat PRIVATE "../common/src")
target_include_directories(igchat PRIVATE "../ncutil/src")

# Compiler flags
set_target_properties(igchat PROPERTIES COMPILE_FLAGS
                      "-Wall -Wextra -Wpedantic -Wshadow -Wpointer-arith \
                       -Wcast-qual -Wno-missing-braces -Wswitch-default \
                       -Wunreachable-code -Wundef -Wuninitialized \
                       -Wcast-align")

# Linking
message(STATUS "Go libraries: ${GO_LIBRARIES}")
target_link_libraries(igchat PUBLIC ref-cgoig ncutil ${GO_LIBRARIES} -ldl -lm)
