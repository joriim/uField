# μField storage: sync planning (pure C++17, no Qt), ADR 0004.
#
# Standalone build for the unit tests:
#   cmake -S ufield/storage -B build/storage && cmake --build build/storage && ctest --test-dir build/storage
cmake_minimum_required(VERSION 3.16)
project(ufield_storage LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(ufield_syncplan STATIC syncplan.cpp)
target_include_directories(ufield_syncplan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(ufield_syncplan PRIVATE -Wall -Wextra -Werror)
endif()

if(PROJECT_IS_TOP_LEVEL)
  enable_testing()
  add_executable(test_syncplan tests/test_syncplan.cpp)
  target_link_libraries(test_syncplan PRIVATE ufield_syncplan)
  add_test(NAME syncplan COMMAND test_syncplan)
endif()
