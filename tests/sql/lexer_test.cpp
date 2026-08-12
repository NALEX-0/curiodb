#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "curiodb/sql/lexer.hpp"

namespace curiodb::sql {
namespace {

std::vector<TokenType> types_of(const std::vector<Token>& tokens) {
  std::vector<TokenType> types;
  types.reserve(tokens.size());
  for (const auto& token : tokens) {
    types.push_back(token.type);
  }
  return types;
}

TEST(LexerTest, TokenizesEmptyInput) {
  const auto tokens = Lexer{""}.tokenize();

  ASSERT_EQ(tokens.size(), 1U);
  EXPECT_EQ(tokens.front(), (Token{TokenType::EndOfInput, "", {0, 1, 1}}));
}

TEST(LexerTest, RecognizesKeywordsWithoutCaseSensitivity) {
  const auto tokens =
      Lexer{"create DATABASE Use table INT double VarChar insert INTO values "
            "select FROM where and OR"}
          .tokenize();

  EXPECT_EQ(types_of(tokens),
            (std::vector<TokenType>{TokenType::Create, TokenType::Database,
                                    TokenType::Use, TokenType::Table,
                                    TokenType::Int, TokenType::Double,
                                    TokenType::Varchar, TokenType::Insert,
                                    TokenType::Into, TokenType::Values,
                                    TokenType::Select, TokenType::From,
                                    TokenType::Where,
                                    TokenType::And, TokenType::Or,
                                    TokenType::EndOfInput}));
}

TEST(LexerTest, RecognizesDeleteStatementKeywords) {
  const auto tokens = Lexer{"DELETE FROM employees WHERE id = 1;"}.tokenize();

  ASSERT_GE(tokens.size(), 4U);
  EXPECT_EQ(tokens[0].type, TokenType::Delete);
  EXPECT_EQ(tokens[1].type, TokenType::From);
  EXPECT_EQ(tokens[3].type, TokenType::Where);
}

TEST(LexerTest, RecognizesUpdateStatementKeywords) {
  const auto tokens =
      Lexer{"UPDATE employees SET name = 'Bob' WHERE id = 1;"}.tokenize();

  ASSERT_GE(tokens.size(), 7U);
  EXPECT_EQ(tokens[0].type, TokenType::Update);
  EXPECT_EQ(tokens[2].type, TokenType::Set);
  EXPECT_EQ(tokens[6].type, TokenType::Where);
}

TEST(LexerTest, KeepsNonKeywordsAsIdentifiers) {
  const auto tokens = Lexer{"employees employee_2 _internal"}.tokenize();

  ASSERT_EQ(tokens.size(), 4U);
  EXPECT_EQ(tokens[0], (Token{TokenType::Identifier, "employees", {0, 1, 1}}));
  EXPECT_EQ(tokens[1].type, TokenType::Identifier);
  EXPECT_EQ(tokens[1].lexeme, "employee_2");
  EXPECT_EQ(tokens[2].type, TokenType::Identifier);
  EXPECT_EQ(tokens[2].lexeme, "_internal");
}

TEST(LexerTest, RecognizesNumbersAndLeavesTrailingDotAsPunctuation) {
  const auto tokens = Lexer{"42 3.1415 12."}.tokenize();

  EXPECT_EQ(types_of(tokens),
            (std::vector<TokenType>{TokenType::IntegerLiteral,
                                    TokenType::FloatingPointLiteral,
                                    TokenType::IntegerLiteral, TokenType::Dot,
                                    TokenType::EndOfInput}));
  EXPECT_EQ(tokens[1].lexeme, "3.1415");
}

TEST(LexerTest, RecognizesStringsAndSqlEscapedQuotes) {
  const auto tokens = Lexer{"'CurioDB' 'O''Brien'"}.tokenize();

  ASSERT_EQ(tokens.size(), 3U);
  EXPECT_EQ(tokens[0].type, TokenType::StringLiteral);
  EXPECT_EQ(tokens[0].lexeme, "'CurioDB'");
  EXPECT_EQ(tokens[1].type, TokenType::StringLiteral);
  EXPECT_EQ(tokens[1].lexeme, "'O''Brien'");
}

TEST(LexerTest, RecognizesPunctuationAndOperators) {
  const auto tokens = Lexer{"(),;. = != <> < <= > >= + - * /"}.tokenize();

  EXPECT_EQ(types_of(tokens),
            (std::vector<TokenType>{
                TokenType::LeftParen, TokenType::RightParen, TokenType::Comma,
                TokenType::Semicolon, TokenType::Dot, TokenType::Equal,
                TokenType::NotEqual, TokenType::NotEqual, TokenType::LessThan,
                TokenType::LessThanOrEqual, TokenType::GreaterThan,
                TokenType::GreaterThanOrEqual, TokenType::Plus,
                TokenType::Minus, TokenType::Star, TokenType::Slash,
                TokenType::EndOfInput}));
}

TEST(LexerTest, TracksLocationsAcrossLines) {
  const auto tokens = Lexer{"CREATE\n  TABLE employees"}.tokenize();

  ASSERT_EQ(tokens.size(), 4U);
  EXPECT_EQ(tokens[0].location, (SourceLocation{0, 1, 1}));
  EXPECT_EQ(tokens[1].location, (SourceLocation{9, 2, 3}));
  EXPECT_EQ(tokens[2].location, (SourceLocation{15, 2, 9}));
  EXPECT_EQ(tokens[3].location, (SourceLocation{24, 2, 18}));
}

TEST(LexerTest, TokenizesCreateTableStatement) {
  const auto tokens =
      Lexer{"CREATE TABLE employees (id INT, name VARCHAR(100));"}.tokenize();

  EXPECT_EQ(types_of(tokens),
            (std::vector<TokenType>{
                TokenType::Create, TokenType::Table, TokenType::Identifier,
                TokenType::LeftParen, TokenType::Identifier, TokenType::Int,
                TokenType::Comma, TokenType::Identifier, TokenType::Varchar,
                TokenType::LeftParen, TokenType::IntegerLiteral,
                TokenType::RightParen, TokenType::RightParen,
                TokenType::Semicolon, TokenType::EndOfInput}));
}

TEST(LexerTest, EmitsInvalidTokensAndContinues) {
  const auto tokens = Lexer{"@ ! CREATE"}.tokenize();

  ASSERT_EQ(tokens.size(), 4U);
  EXPECT_EQ(tokens[0], (Token{TokenType::Invalid, "@", {0, 1, 1}}));
  EXPECT_EQ(tokens[1], (Token{TokenType::Invalid, "!", {2, 1, 3}}));
  EXPECT_EQ(tokens[2].type, TokenType::Create);
}

TEST(LexerTest, MarksUnterminatedStringAsInvalid) {
  const auto tokens = Lexer{"'unfinished\nstring"}.tokenize();

  ASSERT_EQ(tokens.size(), 2U);
  EXPECT_EQ(tokens[0].type, TokenType::Invalid);
  EXPECT_EQ(tokens[0].lexeme, "'unfinished\nstring");
  EXPECT_EQ(tokens[1].location, (SourceLocation{18, 2, 7}));
}

}  // namespace
}  // namespace curiodb::sql
