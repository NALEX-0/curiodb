cmake_minimum_required(VERSION 3.20)

project(
  CurioDB
  VERSION 0.1.0
  DESCRIPTION "A small SQL database build for curiosity"
  LANGUAGES CXX
)

option(CURIODB_BUILD_TESTS "Build CurioDB tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

add_library(curiodb_core
  src/catalog/catalog.cpp
  src/cli/shell.cpp
  src/execution/operators.cpp
  src/execution/statement_executor.cpp
  src/sql/lexer.cpp
  src/sql/parser.cpp
  src/types/data_type.cpp
  src/sql/token.cpp
  src/storage/in_memory_table.cpp
  src/storage/in_memory_storage.cpp
  src/storage/disk_manager.cpp
  src/storage/row_validation.cpp
  src/storage/serialization.cpp
  src/storage/slotted_page.cpp
  src/storage/table_heap.cpp
  src/types/value.cpp
)

add_library(CurioDB::core ALIAS curiodb_core)

target_include_directories(curiodb_core
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(curiodb_core PUBLIC cxx_std_20)
curiodb_set_project_warnings(curiodb_core)

add_executable(curiodb src/main.cpp)
target_link_libraries(curiodb PRIVATE CurioDB::core)
curiodb_set_project_warnings(curiodb)

if(CURIODB_BUILD_TESTS)
  include(CTest)
  if(BUILD_TESTING)
    add_subdirectory(tests)
  endif()
endif()
