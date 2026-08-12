#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "curiodb/sql/ast.hpp"
#include "curiodb/sql/token.hpp"

namespace curiodb::sql {

struct ParseError {
  std::string message;
  SourceLocation location;

  [[nodiscard]] friend bool operator==(const ParseError&, const ParseError&) =
      default;
};

using ParseResult = std::variant<Statement, ParseError>;

class Parser {
 public:
  explicit Parser(std::vector<Token> tokens);

  [[nodiscard]] ParseResult parse_statement();

 private:
  [[nodiscard]] const Token& current() const noexcept;
  [[nodiscard]] bool check(TokenType type) const noexcept;
  bool match(TokenType type) noexcept;
  const Token& advance() noexcept;
  [[nodiscard]] std::optional<Token> consume(TokenType type,
                                              std::string message);
  void report_error(const Token& token, std::string message);

  [[nodiscard]] std::optional<Statement> parse_create();
  [[nodiscard]] std::optional<Statement> parse_create_database(
      SourceLocation statement_location);
  [[nodiscard]] std::optional<Statement> parse_create_table(
      SourceLocation statement_location);
  [[nodiscard]] std::optional<Statement> parse_use();
  [[nodiscard]] std::optional<Statement> parse_insert();
  [[nodiscard]] std::optional<Statement> parse_select();
  [[nodiscard]] std::optional<Statement> parse_delete();
  [[nodiscard]] std::optional<Statement> parse_update();
  [[nodiscard]] std::optional<ColumnDefinition> parse_column();
  [[nodiscard]] std::optional<DataType> parse_data_type();
  [[nodiscard]] std::optional<Literal> parse_literal();
  [[nodiscard]] std::optional<ComparisonExpression> parse_comparison();
  [[nodiscard]] std::optional<Expression> parse_or_expression();
  [[nodiscard]] std::optional<Expression> parse_and_expression();
  [[nodiscard]] std::optional<Expression> parse_expression_primary();

  std::vector<Token> tokens_;
  std::size_t current_index_{0};
  std::optional<ParseError> error_;
};

}  // namespace curiodb::sql
