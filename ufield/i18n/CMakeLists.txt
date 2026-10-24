# μField UI translations, kept apart from upstream's Transifex files (app/i18n) so
# upstream merges never touch them. Finnish here; English is the source language.
# Regenerate after changing strings in ufield/qml, ufield/storage or ufield/tools: scripts/update_ufield_i18n.sh
cmake_minimum_required(VERSION 3.21)
project(ufield_i18n LANGUAGES CXX)

find_package(Qt6 COMPONENTS Core REQUIRED)

add_library(ufield_i18n STATIC ufieldtranslations.cpp)
target_include_directories(ufield_i18n PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ufield_i18n PUBLIC Qt6::Core)
qt_add_resources(ufield_i18n ufield_i18n_qm PREFIX /ufield/i18n FILES ufield_fi.qm)

if(PROJECT_IS_TOP_LEVEL)
  find_package(Qt6 COMPONENTS Test REQUIRED)
  enable_testing()
  set(CMAKE_AUTOMOC ON)
  add_executable(test_translations tests/test_translations.cpp)
  target_link_libraries(test_translations PRIVATE ufield_i18n Qt6::Test)
  add_test(NAME translations COMMAND test_translations)
endif()
