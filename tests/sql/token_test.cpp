#include <string_view>

#include <gtest/gtest.h>

#include "curiodb/sql/token.hpp"

namespace curiodb::sql {
namespace {

TEST(SourceLocationTest, UsesHumanFriendlyDefaults) {
  const SourceLocation location;

  EXPECT_EQ(location.offset, 0U);
  EXPECT_EQ(location.line, 1U);
  EXPECT_EQ(location.column, 1U);
}

TEST(TokenTest, StoresTypeLexemeAndLocation) {
  const Token token{
      .type = TokenType::Identifier,
      .lexeme = "employees",
      .location = {.offset = 13, .line = 2, .column = 7},
  };

  EXPECT_EQ(token.type, TokenType::Identifier);
  EXPECT_EQ(token.lexeme, "employees");
  EXPECT_EQ(token.location, (SourceLocation{13, 2, 7}));
}

TEST(TokenTypeNameTest, ProvidesReadableDiagnosticNames) {
  EXPECT_EQ(token_type_name(TokenType::EndOfInput), "end of input");
  EXPECT_EQ(token_type_name(TokenType::Identifier), "identifier");
  EXPECT_EQ(token_type_name(TokenType::Create), "CREATE");
  EXPECT_EQ(token_type_name(TokenType::LeftParen), "(");
  EXPECT_EQ(token_type_name(TokenType::LessThanOrEqual), "<=");
}

TEST(TokenTypeNameTest, HandlesValuesOutsideTheEnumeration) {
  constexpr auto unknown_type = static_cast<TokenType>(999);

  EXPECT_EQ(token_type_name(unknown_type), std::string_view{"unknown token"});
}

}  // namespace
}  // namespace curiodb::sql

