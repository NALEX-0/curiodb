#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace curiodb::sql {

// Position of the first character of a token. Lines and columns are one-based,
// offset is zero-based so it can index the original SQL string directly
struct SourceLocation {
  std::size_t offset{0};
  std::size_t line{1};
  std::size_t column{1};

  [[nodiscard]] friend constexpr bool operator==(
      const SourceLocation&, const SourceLocation&) = default;
};

enum class TokenType {
  // Special tokens
  EndOfInput,
  Invalid,

  // Names and literal values
  Identifier,
  IntegerLiteral,
  FloatingPointLiteral,
  StringLiteral,

  // SQL keywords. Keywords are recognized case-insensitively by the lexer
  Create,
  Index,
  On,
  Database,
  Use,
  Table,
  Int,
  Double,
  Varchar,
  Primary,
  Key,
  Unique,
  Is,
  Not,
  Null,
  Insert,
  Into,
  Values,
  Select,
  Explain,
  Delete,
  Update,
  Set,
  From,
  Where,
  And,
  Or,

  // Punctuation
  LeftParen,
  RightParen,
  Comma,
  Semicolon,
  Dot,

  // Operators
  Equal,
  NotEqual,
  LessThan,
  LessThanOrEqual,
  GreaterThan,
  GreaterThanOrEqual,
  Plus,
  Minus,
  Star,
  Slash,
};

[[nodiscard]] std::string_view token_type_name(TokenType type) noexcept;

struct Token {
  TokenType type{TokenType::Invalid};
  std::string lexeme;
  SourceLocation location;

  [[nodiscard]] friend bool operator==(const Token&, const Token&) = default;
};

}  // namespace curiodb::sql
