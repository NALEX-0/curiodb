#include <string>
#include <utility>
#include <variant>
#include <vector>

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

  EXPECT_EQ(error.message,
            "expected CREATE, USE, INSERT, or SELECT, found 'employees'");
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

TEST(ParserTest, ParsesInsertWithTypedAndEscapedLiterals) {
  const auto result =
      parse("INSERT INTO employees VALUES (-1, 'O''Brien', 65000.5);");
  const auto& statement = expect_success(result);
  const auto& insert = std::get<InsertStatement>(statement);

  EXPECT_EQ(insert.table_name, "employees");
  ASSERT_EQ(insert.values.size(), 3U);
  EXPECT_EQ(std::get<std::int64_t>(insert.values[0].value), -1);
  EXPECT_EQ(std::get<std::string>(insert.values[1].value), "O'Brien");
  EXPECT_DOUBLE_EQ(std::get<double>(insert.values[2].value), 65000.5);
  EXPECT_EQ(insert.location, (SourceLocation{0, 1, 1}));
}

TEST(ParserTest, RejectsMalformedInsert) {
  EXPECT_EQ(expect_error(parse("INSERT employees VALUES (1);" )).message,
            "expected INTO after INSERT, found 'employees'");
  EXPECT_EQ(expect_error(parse("INSERT INTO employees VALUES ();" )).message,
            "expected at least one value");
  EXPECT_EQ(expect_error(parse("INSERT INTO employees VALUES (name);" )).message,
            "expected integer, floating-point, or string value, found 'name'");
}

TEST(ParserTest, ParsesSelectAll) {
  const auto& statement =
      expect_success(parse("SELECT * FROM employees;"));
  const auto& select = std::get<SelectStatement>(statement);

  EXPECT_TRUE(select.columns.empty());
  EXPECT_EQ(select.table_name, "employees");
  EXPECT_EQ(select.location, (SourceLocation{0, 1, 1}));
}

TEST(ParserTest, RejectsUnsupportedSelectForms) {
  EXPECT_EQ(expect_error(parse("SELECT * employees;")).message,
            "expected FROM after selected columns, found 'employees'");
  EXPECT_EQ(expect_error(parse("SELECT FROM employees;")).message,
            "expected '*' or column name after SELECT, found 'FROM'");
}

TEST(ParserTest, ParsesColumnProjectionInRequestedOrder) {
  const auto& statement =
      expect_success(parse("SELECT name, salary FROM employees;"));
  const auto& select = std::get<SelectStatement>(statement);

  ASSERT_EQ(select.columns.size(), 2U);
  EXPECT_EQ(select.columns[0].name, "name");
  EXPECT_EQ(select.columns[0].location, (SourceLocation{7, 1, 8}));
  EXPECT_EQ(select.columns[1].name, "salary");
  EXPECT_EQ(select.table_name, "employees");
}

TEST(ParserTest, ParsesWhereComparison) {
  const auto& statement = expect_success(
      parse("SELECT name FROM employees WHERE salary >= 70000.0;"));
  const auto& select = std::get<SelectStatement>(statement);

  ASSERT_TRUE(select.where.has_value());
  EXPECT_EQ(select.where->column_name, "salary");
  EXPECT_EQ(select.where->operation, ComparisonOperator::GreaterThanOrEqual);
  EXPECT_DOUBLE_EQ(std::get<double>(select.where->value.value), 70000.0);
  EXPECT_EQ(select.where->location, (SourceLocation{33, 1, 34}));
}

TEST(ParserTest, ParsesAllComparisonOperators) {
  const std::vector<std::pair<std::string, ComparisonOperator>> cases{
      {"=", ComparisonOperator::Equal},
      {"!=", ComparisonOperator::NotEqual},
      {"<>", ComparisonOperator::NotEqual},
      {"<", ComparisonOperator::LessThan},
      {"<=", ComparisonOperator::LessThanOrEqual},
      {">", ComparisonOperator::GreaterThan},
      {">=", ComparisonOperator::GreaterThanOrEqual},
  };

  for (const auto& [text, expected] : cases) {
    const auto statement =
        expect_success(parse("SELECT * FROM employees WHERE id " + text + " 1;"));
    EXPECT_EQ(std::get<SelectStatement>(statement).where->operation, expected);
  }
}

TEST(ParserTest, RejectsMalformedWhereComparison) {
  EXPECT_EQ(expect_error(parse("SELECT * FROM employees WHERE = 1;")).message,
            "expected column name after WHERE, found '='");
  EXPECT_EQ(expect_error(parse("SELECT * FROM employees WHERE id 1;")).message,
            "expected comparison operator after column, found '1'");
}

}  // namespace
}  // namespace curiodb::sql
