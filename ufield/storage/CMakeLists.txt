# μField storage: Google Drive sync (ADR 0004).
#   ufield_syncplan  sync planning rules, pure C++17
#   ufield_sync      Qt layer: project scan, .ufield/sync.json, SyncEngine
#
# Standalone build for the unit tests (Qt 6 Core and Test for the Qt layer):
#   cmake -S ufield/storage -B build/storage && cmake --build build/storage && ctest --test-dir build/storage
cmake_minimum_required(VERSION 3.16)
project(ufield_storage LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(UFIELD_WARNINGS -Wall -Wextra -Werror)
endif()

add_library(ufield_syncplan STATIC syncplan.cpp)
target_include_directories(ufield_syncplan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(ufield_syncplan PRIVATE ${UFIELD_WARNINGS})

find_package(Qt6 COMPONENTS Core)
if(Qt6_FOUND)
  add_library(ufield_sync STATIC syncengine.cpp)
  target_link_libraries(ufield_sync PUBLIC ufield_syncplan Qt6::Core)
  target_compile_options(ufield_sync PRIVATE ${UFIELD_WARNINGS})
endif()

if(PROJECT_IS_TOP_LEVEL)
  enable_testing()
  add_executable(test_syncplan tests/test_syncplan.cpp)
  target_link_libraries(test_syncplan PRIVATE ufield_syncplan)
  add_test(NAME syncplan COMMAND test_syncplan)

  find_package(Qt6 COMPONENTS Test)
  if(TARGET ufield_sync AND Qt6Test_FOUND)
    set(CMAKE_AUTOMOC ON)
    add_executable(test_syncengine tests/test_syncengine.cpp)
    target_link_libraries(test_syncengine PRIVATE ufield_sync Qt6::Test)
    add_test(NAME syncengine COMMAND test_syncengine)
  endif()
endif()
