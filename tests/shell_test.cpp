#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "curiodb/cli/shell.hpp"

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
