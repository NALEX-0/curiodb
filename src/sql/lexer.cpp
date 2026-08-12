#include "curiodb/sql/lexer.hpp"

#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace curiodb::sql {
namespace {

bool is_identifier_start(char character) noexcept {
  const auto value = static_cast<unsigned char>(character);
  return std::isalpha(value) != 0 || character == '_';
}

bool is_identifier_part(char character) noexcept {
  const auto value = static_cast<unsigned char>(character);
  return std::isalnum(value) != 0 || character == '_';
}

bool is_digit(char character) noexcept {
  return std::isdigit(static_cast<unsigned char>(character)) != 0;
}

std::string uppercase(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (const char character : text) {
    result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
  }
  return result;
}

TokenType identifier_type(std::string_view text) {
  static const std::unordered_map<std::string, TokenType> keywords{
      {"CREATE", TokenType::Create},
      {"DATABASE", TokenType::Database},
      {"USE", TokenType::Use},
      {"TABLE", TokenType::Table},
      {"INT", TokenType::Int},
      {"DOUBLE", TokenType::Double},
      {"VARCHAR", TokenType::Varchar},
      {"INSERT", TokenType::Insert},
      {"INTO", TokenType::Into},
      {"VALUES", TokenType::Values},
      {"SELECT", TokenType::Select},
      {"DELETE", TokenType::Delete},
      {"UPDATE", TokenType::Update},
      {"SET", TokenType::Set},
      {"FROM", TokenType::From},
      {"WHERE", TokenType::Where},
      {"AND", TokenType::And},
      {"OR", TokenType::Or},
  };

  const auto keyword = keywords.find(uppercase(text));
  return keyword == keywords.end() ? TokenType::Identifier : keyword->second;
}

}  // namespace

Lexer::Lexer(std::string_view input) : input_(input) {}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokens;
  while (true) {
    skip_whitespace();
    token_start_ = current_;
    token_location_ = {.offset = current_, .line = line_, .column = column_};

    if (at_end()) {
      tokens.push_back(make_token(TokenType::EndOfInput));
      return tokens;
    }

    tokens.push_back(scan_token());
  }
}

bool Lexer::at_end() const noexcept { return current_ >= input_.size(); }

char Lexer::peek(std::size_t lookahead) const noexcept {
  const std::size_t position = current_ + lookahead;
  return position < input_.size() ? input_[position] : '\0';
}

char Lexer::advance() noexcept {
  const char character = input_[current_++];
  if (character == '\n') {
    ++line_;
    column_ = 1;
  } else {
    ++column_;
  }
  return character;
}

bool Lexer::match(char expected) noexcept {
  if (at_end() || peek() != expected) {
    return false;
  }
  advance();
  return true;
}

void Lexer::skip_whitespace() noexcept {
  while (!at_end() && std::isspace(static_cast<unsigned char>(peek())) != 0) {
    advance();
  }
}

Token Lexer::scan_token() {
  const char character = advance();
  if (is_identifier_start(character)) {
    return scan_identifier_or_keyword();
  }
  if (is_digit(character)) {
    return scan_number();
  }

  switch (character) {
    case '\'':
      return scan_string();
    case '(':
      return make_token(TokenType::LeftParen);
    case ')':
      return make_token(TokenType::RightParen);
    case ',':
      return make_token(TokenType::Comma);
    case ';':
      return make_token(TokenType::Semicolon);
    case '.':
      return make_token(TokenType::Dot);
    case '=':
      return make_token(TokenType::Equal);
    case '!':
      return make_token(match('=') ? TokenType::NotEqual : TokenType::Invalid);
    case '<':
      if (match('=')) {
        return make_token(TokenType::LessThanOrEqual);
      }
      return make_token(match('>') ? TokenType::NotEqual : TokenType::LessThan);
    case '>':
      return make_token(match('=') ? TokenType::GreaterThanOrEqual
                                   : TokenType::GreaterThan);
    case '+':
      return make_token(TokenType::Plus);
    case '-':
      return make_token(TokenType::Minus);
    case '*':
      return make_token(TokenType::Star);
    case '/':
      return make_token(TokenType::Slash);
    default:
      return make_token(TokenType::Invalid);
  }
}

Token Lexer::scan_identifier_or_keyword() {
  while (is_identifier_part(peek())) {
    advance();
  }
  const std::string_view text{input_.data() + token_start_,
                              current_ - token_start_};
  return make_token(identifier_type(text));
}

Token Lexer::scan_number() {
  while (is_digit(peek())) {
    advance();
  }

  TokenType type = TokenType::IntegerLiteral;
  if (peek() == '.' && is_digit(peek(1))) {
    type = TokenType::FloatingPointLiteral;
    advance();
    while (is_digit(peek())) {
      advance();
    }
  }
  return make_token(type);
}

Token Lexer::scan_string() {
  while (!at_end()) {
    if (peek() == '\'') {
      advance();
      if (match('\'')) {
        continue;
      }
      return make_token(TokenType::StringLiteral);
    }
    advance();
  }
  return make_token(TokenType::Invalid);
}

Token Lexer::make_token(TokenType type) const {
  return {
      .type = type,
      .lexeme = input_.substr(token_start_, current_ - token_start_),
      .location = token_location_,
  };
}

}  // namespace curiodb::sql
