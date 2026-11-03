cmake_minimum_required(VERSION 3.24)

project(gaffer_search
  VERSION 0.1.0
  DESCRIPTION "Ask the Gaffer: a from-scratch search engine over a Premier League corpus"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# clang-tidy and editors read compile_commands.json.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(GAFFER_BUILD_TESTS "Build the GoogleTest suites" ON)
option(GAFFER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
set(GAFFER_SANITIZE "" CACHE STRING
  "Comma-separated sanitizers to enable, e.g. 'address,undefined' or 'thread'")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)

# Sanitizers are applied globally (before any target, including GoogleTest) so
# that every translation unit linked into a test binary is instrumented the
# same way. Mixing instrumented and uninstrumented code causes false reports.
gaffer_enable_sanitizers("${GAFFER_SANITIZE}")

add_subdirectory(src)
add_subdirectory(apps)

if(GAFFER_BUILD_TESTS)
  enable_testing()
  include(FetchGoogleTest)
  add_subdirectory(tests)
endif()
