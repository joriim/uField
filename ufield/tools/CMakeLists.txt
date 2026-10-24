# μField ToolDispatcher in the app (CLAUDE.md rule 1): schema/tools.json embedded as
# :/ufield/schema/tools.json, validation with the same results as the Python dispatcher
# (tested against schema/tests/cases.json), and the handlers of app-side tools.
#
# Standalone build for the unit tests (builds ufield/storage too):
#   cmake -S ufield/tools -B build/tools && cmake --build build/tools && ctest --test-dir build/tools
cmake_minimum_required(VERSION 3.21)
project(ufield_tools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(UFIELD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

find_package(Qt6 COMPONENTS Core REQUIRED)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(UFIELD_WARNINGS -Wall -Wextra)
  if(PROJECT_IS_TOP_LEVEL)
    list(APPEND UFIELD_WARNINGS -Werror)
  endif()
endif()

set(CMAKE_AUTOMOC ON)
add_library(ufield_tools STATIC toolschema.cpp tooldispatcher.cpp)
set(CMAKE_AUTOMOC OFF)
target_include_directories(ufield_tools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ufield_tools PUBLIC Qt6::Core)
target_compile_options(ufield_tools PRIVATE ${UFIELD_WARNINGS})
qt_add_resources(ufield_tools ufield_tools_schema PREFIX /ufield/schema BASE ${UFIELD_ROOT}/schema FILES ${UFIELD_ROOT}/schema/tools.json)

# Handlers of the storage tools (list_projects, download_project, sync_project), on DriveSync.
if(PROJECT_IS_TOP_LEVEL AND NOT TARGET ufield_sync)
  add_subdirectory(${UFIELD_ROOT}/ufield/storage ${CMAKE_BINARY_DIR}/storage)
endif()
add_library(ufield_storagetools STATIC storagetools.cpp)
target_link_libraries(ufield_storagetools PUBLIC ufield_tools ufield_sync)
target_compile_options(ufield_storagetools PRIVATE ${UFIELD_WARNINGS})

if(PROJECT_IS_TOP_LEVEL)
  find_package(Qt6 COMPONENTS Network Test REQUIRED)
  enable_testing()
  set(CMAKE_AUTOMOC ON)
  add_executable(test_toolschema tests/test_toolschema.cpp)
  target_link_libraries(test_toolschema PRIVATE ufield_tools Qt6::Test)
  target_compile_definitions(test_toolschema PRIVATE UFIELD_CASES_JSON="${UFIELD_ROOT}/schema/tests/cases.json")
  add_test(NAME toolschema COMMAND test_toolschema)

  add_executable(test_storagetools tests/test_storagetools.cpp ${UFIELD_ROOT}/ufield/storage/tests/fakedrive.h)
  target_include_directories(test_storagetools PRIVATE ${UFIELD_ROOT}/ufield/storage/tests)
  target_link_libraries(test_storagetools PRIVATE ufield_storagetools Qt6::Test Qt6::Network)
  add_test(NAME storagetools COMMAND test_storagetools)
endif()
