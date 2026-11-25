cmake_minimum_required(VERSION 3.20)
project(memlayout VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(memlayout_core STATIC
  src/clang_dump.cpp
  src/layout.cpp
  src/figure.cpp
  src/process.cpp
  src/render_svg.cpp
  src/render_term.cpp
  src/render_json.cpp
  src/cli.cpp)
target_include_directories(memlayout_core PUBLIC src)
target_compile_options(memlayout_core PUBLIC -Wall -Wextra -Wpedantic)
target_compile_definitions(memlayout_core PUBLIC MEMLAYOUT_VERSION="${PROJECT_VERSION}")

add_executable(memlayout src/main.cpp)
target_link_libraries(memlayout PRIVATE memlayout_core)

# Release binaries on Linux must not depend on the build machine's libstdc++.
option(MEMLAYOUT_STATIC "Link libstdc++/libgcc statically (Linux release builds)" OFF)
if(MEMLAYOUT_STATIC AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_options(memlayout PRIVATE -static-libstdc++ -static-libgcc)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(memlayout_tests tests/tests.cpp)
  target_link_libraries(memlayout_tests PRIVATE memlayout_core)
  add_test(NAME unit COMMAND memlayout_tests)
  add_test(NAME e2e_basic
    COMMAND memlayout ${CMAKE_SOURCE_DIR}/examples/basic.cpp Sample --format table --no-probe)
endif()

install(TARGETS memlayout RUNTIME DESTINATION bin)
