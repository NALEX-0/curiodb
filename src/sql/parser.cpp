#include "curiodb/sql/parser.hpp"

#include <charconv>
#include <cstddef>
#include <string>
#include <system_error>
#include <utility>
#include <variant>

namespace curiodb::sql {
namespace {

std::string found_token(const Token& token) {
  if (token.type == TokenType::EndOfInput) {
    return "end of input";
  }
  return "'" + token.lexeme + "'";
}

}  // namespace

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {
  if (tokens_.empty() || tokens_.back().type != TokenType::EndOfInput) {
    tokens_.push_back(Token{TokenType::EndOfInput, "", {}});
  }
}

ParseResult Parser::parse_statement() {
  std::optional<Statement> statement;
  if (check(TokenType::Create)) {
    statement = parse_create();
  } else if (check(TokenType::Use)) {
    statement = parse_use();
  } else if (check(TokenType::Invalid)) {
    report_error(current(), "invalid token " + found_token(current()));
  } else {
    report_error(current(), "expected CREATE or USE, found " +
                                found_token(current()));
  }

  if (statement.has_value() &&
      !consume(TokenType::Semicolon, "expected ';' after statement")) {
    statement.reset();
  }
  if (statement.has_value() && !check(TokenType::EndOfInput)) {
    report_error(current(), "expected end of input, found " +
                                found_token(current()));
    statement.reset();
  }

  if (statement.has_value()) {
    return std::move(*statement);
  }
  return *error_;
}

const Token& Parser::current() const noexcept { return tokens_[current_index_]; }

bool Parser::check(TokenType type) const noexcept {
  return current().type == type;
}

bool Parser::match(TokenType type) noexcept {
  if (!check(type)) {
    return false;
  }
  advance();
  return true;
}

const Token& Parser::advance() noexcept {
  const Token& token = current();
  if (!check(TokenType::EndOfInput)) {
    ++current_index_;
  }
  return token;
}

std::optional<Token> Parser::consume(TokenType type, std::string message) {
  if (check(type)) {
    return advance();
  }
  report_error(current(), std::move(message) + ", found " +
                              found_token(current()));
  return std::nullopt;
}

void Parser::report_error(const Token& token, std::string message) {
  if (!error_.has_value()) {
    error_ = ParseError{std::move(message), token.location};
  }
}

std::optional<Statement> Parser::parse_create() {
  const SourceLocation location = advance().location;
  if (match(TokenType::Database)) {
    return parse_create_database(location);
  }
  if (match(TokenType::Table)) {
    return parse_create_table(location);
  }
  report_error(current(), "expected DATABASE or TABLE after CREATE, found " +
                              found_token(current()));
  return std::nullopt;
}

std::optional<Statement> Parser::parse_create_database(
    SourceLocation statement_location) {
  const auto name = consume(TokenType::Identifier,
                            "expected database name after DATABASE");
  if (!name.has_value()) {
    return std::nullopt;
  }
  return Statement{CreateDatabaseStatement{
      .name = name->lexeme,
      .location = statement_location,
  }};
}

std::optional<Statement> Parser::parse_create_table(
    SourceLocation statement_location) {
  const auto name =
      consume(TokenType::Identifier, "expected table name after TABLE");
  if (!name.has_value() ||
      !consume(TokenType::LeftParen, "expected '(' after table name")) {
    return std::nullopt;
  }

  if (check(TokenType::RightParen)) {
    report_error(current(), "expected at least one column definition");
    return std::nullopt;
  }

  std::vector<ColumnDefinition> columns;
  do {
    auto column = parse_column();
    if (!column.has_value()) {
      return std::nullopt;
    }
    columns.push_back(std::move(*column));
  } while (match(TokenType::Comma));

  if (!consume(TokenType::RightParen, "expected ')' after column definitions")) {
    return std::nullopt;
  }

  return Statement{CreateTableStatement{
      .name = name->lexeme,
      .columns = std::move(columns),
      .location = statement_location,
  }};
}

std::optional<Statement> Parser::parse_use() {
  const SourceLocation location = advance().location;
  const auto name =
      consume(TokenType::Identifier, "expected database name after USE");
  if (!name.has_value()) {
    return std::nullopt;
  }
  return Statement{UseDatabaseStatement{
      .name = name->lexeme,
      .location = location,
  }};
}

std::optional<ColumnDefinition> Parser::parse_column() {
  const auto name = consume(TokenType::Identifier, "expected column name");
  if (!name.has_value()) {
    return std::nullopt;
  }
  auto type = parse_data_type();
  if (!type.has_value()) {
    return std::nullopt;
  }
  return ColumnDefinition{
      .name = name->lexeme,
      .type = std::move(*type),
      .location = name->location,
  };
}

std::optional<DataType> Parser::parse_data_type() {
  if (match(TokenType::Int)) {
    return DataType{.kind = DataTypeKind::Integer};
  }
  if (match(TokenType::Double)) {
    return DataType{.kind = DataTypeKind::Double};
  }
  if (!match(TokenType::Varchar)) {
    report_error(current(), "expected INT, DOUBLE, or VARCHAR, found " +
                                found_token(current()));
    return std::nullopt;
  }

  if (!consume(TokenType::LeftParen, "expected '(' after VARCHAR")) {
    return std::nullopt;
  }
  const auto length_token =
      consume(TokenType::IntegerLiteral, "expected VARCHAR length");
  if (!length_token.has_value()) {
    return std::nullopt;
  }

  std::size_t length = 0;
  const char* const begin = length_token->lexeme.data();
  const char* const end = begin + length_token->lexeme.size();
  const auto conversion = std::from_chars(begin, end, length);
  if (conversion.ec != std::errc{} || conversion.ptr != end || length == 0) {
    report_error(*length_token, "VARCHAR length must be a positive integer");
    return std::nullopt;
  }
  if (!consume(TokenType::RightParen, "expected ')' after VARCHAR length")) {
    return std::nullopt;
  }
  return DataType{.kind = DataTypeKind::Varchar, .length = length};
}

}  // namespace curiodb::sql

