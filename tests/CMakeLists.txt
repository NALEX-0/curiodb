include(FetchContent)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(curiodb_tests
  shell_test.cpp
  sql/ast_test.cpp
  sql/lexer_test.cpp
  sql/parser_test.cpp
  sql/token_test.cpp
  version_test.cpp
)

target_link_libraries(curiodb_tests
  PRIVATE
    CurioDB::core
    GTest::gtest_main
)

curiodb_set_project_warnings(curiodb_tests)

include(GoogleTest)
gtest_discover_tests(curiodb_tests)
