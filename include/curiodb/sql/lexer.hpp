#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "curiodb/sql/token.hpp"

namespace curiodb::sql {

class Lexer {
 public:
  explicit Lexer(std::string_view input);

  [[nodiscard]] std::vector<Token> tokenize();

 private:
  [[nodiscard]] bool at_end() const noexcept;
  [[nodiscard]] char peek(std::size_t lookahead = 0) const noexcept;
  char advance() noexcept;
  bool match(char expected) noexcept;

  void skip_whitespace() noexcept;
  [[nodiscard]] Token scan_token();
  [[nodiscard]] Token scan_identifier_or_keyword();
  [[nodiscard]] Token scan_number();
  [[nodiscard]] Token scan_string();
  [[nodiscard]] Token make_token(TokenType type) const;

  std::string input_;
  std::size_t current_{0};
  std::size_t line_{1};
  std::size_t column_{1};
  std::size_t token_start_{0};
  SourceLocation token_location_;
};

}  // namespace curiodb::sql

