#include <string>
#include <variant>

#include <gtest/gtest.h>

#include "curiodb/sql/ast.hpp"
#include "curiodb/sql/lexer.hpp"
#include "curiodb/sql/parser.hpp"

namespace curiodb::sql {
namespace {

ParseResult parse(std::string input) {
  return Parser{Lexer{input}.tokenize()}.parse_statement();
}

Statement expect_success(const ParseResult& result) {
  EXPECT_TRUE(std::holds_alternative<Statement>(result));
  return std::get<Statement>(result);
}

ParseError expect_error(const ParseResult& result) {
  EXPECT_TRUE(std::holds_alternative<ParseError>(result));
  return std::get<ParseError>(result);
}

TEST(ParserTest, ParsesCreateDatabase) {
  const auto result = parse("CREATE DATABASE company;");
  const auto& statement = expect_success(result);

  const auto& create = std::get<CreateDatabaseStatement>(statement);
  EXPECT_EQ(create.name, "company");
  EXPECT_EQ(create.location, (SourceLocation{0, 1, 1}));
}

TEST(ParserTest, ParsesUseDatabaseCaseInsensitively) {
  const auto result = parse("use company;");
  const auto& statement = expect_success(result);

  EXPECT_EQ(std::get<UseDatabaseStatement>(statement).name, "company");
}

TEST(ParserTest, ParsesCreateTable) {
  const auto result = parse(R"(CREATE TABLE employees (
  id INT,
  name VARCHAR(100),
  salary DOUBLE
);)");
  const auto& statement = expect_success(result);
  const auto& table = std::get<CreateTableStatement>(statement);

  EXPECT_EQ(table.name, "employees");
  ASSERT_EQ(table.columns.size(), 3U);
  EXPECT_EQ(table.columns[0].name, "id");
  EXPECT_EQ(table.columns[0].type,
            (DataType{.kind = DataTypeKind::Integer}));
  EXPECT_EQ(table.columns[1].name, "name");
  EXPECT_EQ(table.columns[1].type,
            (DataType{.kind = DataTypeKind::Varchar, .length = 100}));
  EXPECT_EQ(table.columns[2].type,
            (DataType{.kind = DataTypeKind::Double}));
  EXPECT_EQ(table.columns[1].location, (SourceLocation{37, 3, 3}));
}

TEST(ParserTest, RequiresSemicolon) {
  const auto& error = expect_error(parse("USE company"));

  EXPECT_EQ(error.message, "expected ';' after statement, found end of input");
  EXPECT_EQ(error.location, (SourceLocation{11, 1, 12}));
}

TEST(ParserTest, RejectsUnsupportedStatement) {
  const auto& error = expect_error(parse("employees;"));

  EXPECT_EQ(error.message, "expected CREATE or USE, found 'employees'");
  EXPECT_EQ(error.location, (SourceLocation{0, 1, 1}));
}

TEST(ParserTest, RejectsInvalidLexerToken) {
  const auto& error = expect_error(parse("@;"));

  EXPECT_EQ(error.message, "invalid token '@'");
  EXPECT_EQ(error.location, (SourceLocation{0, 1, 1}));
}

TEST(ParserTest, RequiresCreateTarget) {
  const auto& error = expect_error(parse("CREATE company;"));

  EXPECT_EQ(error.message,
            "expected DATABASE or TABLE after CREATE, found 'company'");
}

TEST(ParserTest, RejectsEmptyTableDefinition) {
  const auto& error = expect_error(parse("CREATE TABLE empty ();"));

  EXPECT_EQ(error.message, "expected at least one column definition");
}

TEST(ParserTest, RejectsUnknownColumnType) {
  const auto& error =
      expect_error(parse("CREATE TABLE employees (id BOOLEAN);"));

  EXPECT_EQ(error.message,
            "expected INT, DOUBLE, or VARCHAR, found 'BOOLEAN'");
}

TEST(ParserTest, ValidatesVarcharLength) {
  const auto& zero =
      expect_error(parse("CREATE TABLE employees (name VARCHAR(0));"));
  EXPECT_EQ(zero.message, "VARCHAR length must be a positive integer");

  const auto& missing =
      expect_error(parse("CREATE TABLE employees (name VARCHAR);"));
  EXPECT_EQ(missing.message, "expected '(' after VARCHAR, found ')'");
}

TEST(ParserTest, RejectsTrailingTokens) {
  const auto& error = expect_error(parse("USE company; USE other;"));

  EXPECT_EQ(error.message, "expected end of input, found 'USE'");
  EXPECT_EQ(error.location, (SourceLocation{13, 1, 14}));
}

}  // namespace
}  // namespace curiodb::sql
