# μField storage: Google Drive sync (ADR 0004).
#   ufield_syncplan  sync planning rules, pure C++17
#   ufield_sync      Qt layer: project scan, .ufield/sync.json, SyncEngine,
#                    GoogleDriveProvider (Drive REST v3)
#
# Standalone build for the unit tests (Qt 6 Core, Network and Test for the Qt layer):
#   cmake -S ufield/storage -B build/storage && cmake --build build/storage && ctest --test-dir build/storage
cmake_minimum_required(VERSION 3.21) # PROJECT_IS_TOP_LEVEL
project(ufield_storage LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Warnings are errors in the standalone test build only, so a new compiler in the app build can't break it.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(UFIELD_WARNINGS -Wall -Wextra)
  if(PROJECT_IS_TOP_LEVEL)
    list(APPEND UFIELD_WARNINGS -Werror)
  endif()
endif()

add_library(ufield_syncplan STATIC syncplan.cpp)
target_include_directories(ufield_syncplan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(ufield_syncplan PRIVATE ${UFIELD_WARNINGS})

find_package(Qt6 COMPONENTS Core Network)
if(Qt6Core_FOUND AND Qt6Network_FOUND)
  add_library(ufield_sync STATIC syncengine.cpp googledriveprovider.cpp driveauth.cpp)
  target_link_libraries(ufield_sync PUBLIC ufield_syncplan Qt6::Core Qt6::Network)
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

    add_executable(test_googledrive tests/test_googledrive.cpp)
    target_link_libraries(test_googledrive PRIVATE ufield_sync Qt6::Test)
    add_test(NAME googledrive COMMAND test_googledrive)
  endif()
endif()
