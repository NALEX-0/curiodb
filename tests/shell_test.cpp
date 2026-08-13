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

TEST(ShellTest, EnforcesAndPersistsPrimaryKeyAndUniqueConstraints) {
  TemporaryDataDirectory directory;
  {
    std::istringstream input{R"(CREATE DATABASE app;
USE app;
CREATE TABLE users (id INT PRIMARY KEY, email VARCHAR(100) UNIQUE);
INSERT INTO users VALUES (1, 'alice@example.com');
INSERT INTO users VALUES (1, 'other@example.com');
INSERT INTO users VALUES (2, 'alice@example.com');
INSERT INTO users VALUES (2, 'bob@example.com');
UPDATE users SET email = 'alice@example.com' WHERE id = 2;
EXPLAIN SELECT email FROM users WHERE id = 1;
.schema users
.quit
)"};
    std::ostringstream output;
    curiodb::cli::Shell shell{input, output, directory.path()};
    ASSERT_EQ(shell.run(), 0) << output.str();
    const std::string result = output.str();
    EXPECT_NE(result.find("duplicate value for PRIMARY KEY 'id'"),
              std::string::npos) << result;
    EXPECT_NE(result.find("duplicate value for UNIQUE column 'email'"),
              std::string::npos) << result;
    EXPECT_NE(result.find("IndexScan("), std::string::npos) << result;
    EXPECT_NE(result.find("id INT PRIMARY KEY"), std::string::npos) << result;
    EXPECT_NE(result.find("email VARCHAR(100) UNIQUE"), std::string::npos)
        << result;
  }

  std::istringstream input{R"(USE app;
INSERT INTO users VALUES (1, 'again@example.com');
INSERT INTO users VALUES (3, 'alice@example.com');
SELECT * FROM users;
.schema users
.quit
)"};
  std::ostringstream output;
  curiodb::cli::Shell reopened{input, output, directory.path()};
  ASSERT_EQ(reopened.run(), 0) << output.str();
  EXPECT_NE(output.str().find("id INT PRIMARY KEY"), std::string::npos);
  EXPECT_NE(output.str().find("2 rows selected."), std::string::npos);
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

TEST(ShellTest, UpdatesMatchingRowsAndPersistsChanges) {
  TemporaryDataDirectory directory;
  {
    std::istringstream input{R"(CREATE DATABASE company;
USE company;
CREATE TABLE employees (id INT, name VARCHAR(100), salary DOUBLE);
INSERT INTO employees VALUES (1, 'Alice', 40000.0);
INSERT INTO employees VALUES (2, 'Bob', 50000.0);
UPDATE employees SET name = 'Robert' WHERE id = 2;
UPDATE employees SET salary = 60000.0 WHERE id >= 1;
.quit
)"};
    std::ostringstream output;
    curiodb::cli::Shell shell{input, output, directory.path()};
    ASSERT_EQ(shell.run(), 0) << output.str();
    EXPECT_NE(output.str().find("1 row updated."), std::string::npos);
    EXPECT_NE(output.str().find("2 rows updated."), std::string::npos);
  }

  std::istringstream input{R"(USE company;
SELECT * FROM employees;
.quit
)"};
  std::ostringstream output;
  curiodb::cli::Shell reopened{input, output, directory.path()};
  ASSERT_EQ(reopened.run(), 0) << output.str();
  EXPECT_NE(output.str().find("Alice"), std::string::npos);
  EXPECT_NE(output.str().find("Robert"), std::string::npos);
  EXPECT_NE(output.str().find("60000"), std::string::npos);
}

TEST(ShellTest, CreatesPersistentIndexAndMaintainsNewInserts) {
  TemporaryDataDirectory directory;
  {
    std::istringstream input{R"(CREATE DATABASE company;
USE company;
CREATE TABLE employees (id INT, name VARCHAR(100));
INSERT INTO employees VALUES (1, 'Alice');
CREATE INDEX employees_id_idx ON employees (id);
INSERT INTO employees VALUES (2, 'Bob');
INSERT INTO employees VALUES (2, 'Duplicate');
.quit
)"};
    std::ostringstream output;
    curiodb::cli::Shell shell{input, output, directory.path()};
    ASSERT_EQ(shell.run(), 0) << output.str();
    EXPECT_NE(output.str().find("Index 'employees_id_idx' created."),
              std::string::npos);
    EXPECT_NE(output.str().find("duplicate value for unique index"),
              std::string::npos);
  }

  std::istringstream input{R"(USE company;
INSERT INTO employees VALUES (1, 'Duplicate after reopen');
SELECT * FROM employees;
.quit
)"};
  std::ostringstream output;
  curiodb::cli::Shell reopened{input, output, directory.path()};
  ASSERT_EQ(reopened.run(), 0) << output.str();
  EXPECT_NE(output.str().find("duplicate value for unique index"),
            std::string::npos);
  EXPECT_NE(output.str().find("Alice"), std::string::npos);
  EXPECT_NE(output.str().find("Bob"), std::string::npos);
  EXPECT_NE(output.str().find("2 rows selected."), std::string::npos);
}

TEST(ShellTest, UsesAndMaintainsIndexAcrossMutations) {
  TemporaryDataDirectory directory;
  std::istringstream input{R"(CREATE DATABASE company;
USE company;
CREATE TABLE employees (id INT, name VARCHAR(100));
INSERT INTO employees VALUES (1, 'Alice');
INSERT INTO employees VALUES (2, 'Bob');
CREATE INDEX employees_id_idx ON employees (id);
EXPLAIN SELECT name FROM employees WHERE id = 2;
SELECT name FROM employees WHERE id = 2;
DELETE FROM employees WHERE id = 1;
INSERT INTO employees VALUES (1, 'Ann');
UPDATE employees SET id = 3 WHERE id = 2;
INSERT INTO employees VALUES (2, 'Ben');
SELECT * FROM employees WHERE id = 3;
SELECT * FROM employees;
.quit
)"};
  std::ostringstream output;
  curiodb::cli::Shell shell{input, output, directory.path()};

  ASSERT_EQ(shell.run(), 0) << output.str();
  const std::string result = output.str();
  EXPECT_NE(result.find("IndexScan(index=employees_id_idx, condition=id = 2)"),
            std::string::npos);
  EXPECT_NE(result.find("Bob"), std::string::npos);
  EXPECT_NE(result.find("Ann"), std::string::npos);
  EXPECT_NE(result.find("Ben"), std::string::npos);
  EXPECT_EQ(result.find("duplicate value for unique index"), std::string::npos);
}
