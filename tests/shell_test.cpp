#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

#include "curiodb/cli/shell.hpp"

namespace {

class TemporaryDataDirectory {
 public:
  TemporaryDataDirectory() {
    const auto suffix = std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count();
    path_ = std::filesystem::temp_directory_path() /
            ("curiodb_shell_test_" + std::to_string(suffix));
  }

  ~TemporaryDataDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  [[nodiscard]] const std::filesystem::path& path() const noexcept {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

}  // namespace

TEST(ShellTest, DisplaysPromptAndQuitsCleanly) {
  std::istringstream input{".quit\n"};
  std::ostringstream output;
  curiodb::cli::Shell shell{input, output};

  EXPECT_EQ(shell.run(), 0);
  EXPECT_NE(output.str().find("CurioDB v0.1.0"), std::string::npos);
  EXPECT_NE(output.str().find("curiodb> "), std::string::npos);
}

TEST(ShellTest, ShowsHelp) {
  std::istringstream input{".help\n.quit\n"};
  std::ostringstream output;
  curiodb::cli::Shell shell{input, output};

  EXPECT_EQ(shell.run(), 0);
  EXPECT_NE(output.str().find(".help"), std::string::npos);
  EXPECT_NE(output.str().find("Show this help"), std::string::npos);
}

TEST(ShellTest, ExecutesSqlAndCatalogCommands) {
  std::istringstream input{R"(CREATE DATABASE company;
USE company;
CREATE TABLE employees (
id INT,
name VARCHAR(100)
);
INSERT INTO employees VALUES (1, 'Alice');
SELECT * FROM employees;
SELECT name, id FROM employees;
SELECT name FROM employees WHERE id = 1;
.databases
.tables
.schema employees
.quit
)"};
  std::ostringstream output;
  curiodb::cli::Shell shell{input, output};

  EXPECT_EQ(shell.run(), 0);
  const std::string result = output.str();
  EXPECT_NE(result.find("Database 'company' created."), std::string::npos);
  EXPECT_NE(result.find("Using database 'company'."), std::string::npos);
  EXPECT_NE(result.find("Table 'employees' created."), std::string::npos);
  EXPECT_NE(result.find("1 row inserted."), std::string::npos);
  EXPECT_NE(result.find("id | name"), std::string::npos);
  EXPECT_NE(result.find("1  | Alice"), std::string::npos);
  EXPECT_NE(result.find("1 row selected."), std::string::npos);
  EXPECT_NE(result.find("name  | id"), std::string::npos);
  EXPECT_NE(result.find("Alice | 1"), std::string::npos);
  EXPECT_NE(result.find("Alice\n1 row selected."), std::string::npos);
  EXPECT_NE(result.find("CREATE TABLE employees (\n"
                        "  id INT,\n"
                        "  name VARCHAR(100)\n"
                        ");"),
            std::string::npos);
}

TEST(ShellTest, RejectsInsertThatDoesNotMatchSchema) {
  std::istringstream input{R"(CREATE DATABASE company;
USE company;
CREATE TABLE employees (id INT);
INSERT INTO employees VALUES ('wrong');
.quit
)"};
  std::ostringstream output;
  curiodb::cli::Shell shell{input, output};

  EXPECT_EQ(shell.run(), 0);
  EXPECT_NE(output.str().find(
                "Error: column 'id': expected INT, received VARCHAR"),
            std::string::npos);
}

TEST(ShellTest, DisplaysParseAndCatalogErrors) {
  std::istringstream input{"USE missing;\nCREATE DATABASE company\n"};
  std::ostringstream output;
  curiodb::cli::Shell shell{input, output};

  EXPECT_EQ(shell.run(), 0);
  EXPECT_NE(output.str().find("Error: database 'missing' does not exist"),
            std::string::npos);
  EXPECT_NE(output.str().find("expected ';' after statement"),
            std::string::npos);
}

TEST(ShellTest, PersistsTablesAndRowsAcrossShellRestarts) {
  TemporaryDataDirectory directory;
  {
    std::istringstream input{R"(CREATE DATABASE company;
USE company;
CREATE TABLE employees (id INT, name VARCHAR(100));
INSERT INTO employees VALUES (1, 'Alice');
INSERT INTO employees VALUES (2, 'Bob');
.quit
)"};
    std::ostringstream output;
    curiodb::cli::Shell shell{input, output, directory.path()};
    ASSERT_EQ(shell.run(), 0) << output.str();
  }

  std::istringstream input{R"(.databases
USE company;
.tables
SELECT name FROM employees WHERE id >= 2;
.quit
)"};
  std::ostringstream output;
  curiodb::cli::Shell reopened{input, output, directory.path()};

  ASSERT_EQ(reopened.run(), 0) << output.str();
  const std::string result = output.str();
  EXPECT_NE(result.find("company"), std::string::npos);
  EXPECT_NE(result.find("employees"), std::string::npos);
  EXPECT_NE(result.find("Bob"), std::string::npos) << result;
  EXPECT_NE(result.find("1 row selected."), std::string::npos) << result;
  EXPECT_EQ(result.find("Alice"), std::string::npos) << result;
}

TEST(ShellTest, DeletesMatchingRowsAndPersistsDeletion) {
  TemporaryDataDirectory directory;
  {
    std::istringstream input{R"(CREATE DATABASE company;
USE company;
CREATE TABLE employees (id INT, name VARCHAR(100));
INSERT INTO employees VALUES (1, 'Alice');
INSERT INTO employees VALUES (2, 'Bob');
INSERT INTO employees VALUES (3, 'Carol');
DELETE FROM employees WHERE id = 1 OR id >= 3;
SELECT * FROM employees;
.quit
)"};
    std::ostringstream output;
    curiodb::cli::Shell shell{input, output, directory.path()};
    ASSERT_EQ(shell.run(), 0) << output.str();
    EXPECT_NE(output.str().find("2 rows deleted."), std::string::npos);
    EXPECT_NE(output.str().find("Bob"), std::string::npos);
  }

  std::istringstream input{R"(USE company;
SELECT * FROM employees;
DELETE FROM employees;
SELECT * FROM employees;
.quit
)"};
  std::ostringstream output;
  curiodb::cli::Shell reopened{input, output, directory.path()};
  ASSERT_EQ(reopened.run(), 0) << output.str();
  EXPECT_NE(output.str().find("1 row deleted."), std::string::npos);
  EXPECT_NE(output.str().find("0 rows selected."), std::string::npos);
}
