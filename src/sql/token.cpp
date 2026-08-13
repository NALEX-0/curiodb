#include "curiodb/sql/token.hpp"

namespace curiodb::sql {

std::string_view token_type_name(TokenType type) noexcept {
  switch (type) {
    case TokenType::EndOfInput:
      return "end of input";
    case TokenType::Invalid:
      return "invalid token";
    case TokenType::Identifier:
      return "identifier";
    case TokenType::IntegerLiteral:
      return "integer literal";
    case TokenType::FloatingPointLiteral:
      return "floating-point literal";
    case TokenType::StringLiteral:
      return "string literal";
    case TokenType::Create:
      return "CREATE";
    case TokenType::Index:
      return "INDEX";
    case TokenType::On:
      return "ON";
    case TokenType::Database:
      return "DATABASE";
    case TokenType::Use:
      return "USE";
    case TokenType::Table:
      return "TABLE";
    case TokenType::Int:
      return "INT";
    case TokenType::Double:
      return "DOUBLE";
    case TokenType::Varchar:
      return "VARCHAR";
    case TokenType::Primary:
      return "PRIMARY";
    case TokenType::Key:
      return "KEY";
    case TokenType::Unique:
      return "UNIQUE";
    case TokenType::Is:
      return "IS";
    case TokenType::Not:
      return "NOT";
    case TokenType::Null:
      return "NULL";
    case TokenType::Insert:
      return "INSERT";
    case TokenType::Into:
      return "INTO";
    case TokenType::Values:
      return "VALUES";
    case TokenType::Select:
      return "SELECT";
    case TokenType::Explain:
      return "EXPLAIN";
    case TokenType::Delete:
      return "DELETE";
    case TokenType::Update:
      return "UPDATE";
    case TokenType::Set:
      return "SET";
    case TokenType::From:
      return "FROM";
    case TokenType::Where:
      return "WHERE";
    case TokenType::And:
      return "AND";
    case TokenType::Or:
      return "OR";
    case TokenType::LeftParen:
      return "(";
    case TokenType::RightParen:
      return ")";
    case TokenType::Comma:
      return ",";
    case TokenType::Semicolon:
      return ";";
    case TokenType::Dot:
      return ".";
    case TokenType::Equal:
      return "=";
    case TokenType::NotEqual:
      return "!=";
    case TokenType::LessThan:
      return "<";
    case TokenType::LessThanOrEqual:
      return "<=";
    case TokenType::GreaterThan:
      return ">";
    case TokenType::GreaterThanOrEqual:
      return ">=";
    case TokenType::Plus:
      return "+";
    case TokenType::Minus:
      return "-";
    case TokenType::Star:
      return "*";
    case TokenType::Slash:
      return "/";
  }

  return "unknown token";
}

}  // namespace curiodb::sql
