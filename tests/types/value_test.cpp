#include <compare>
#include <cstdint>
#include <string>
#include <variant>

#include <gtest/gtest.h>

#include "curiodb/types/data_type.hpp"
#include "curiodb/types/value.hpp"

namespace curiodb {
namespace {

TEST(ValueTest, StoresAndAccessesEachSupportedType) {
  const Value integer{std::int64_t{42}};
  const Value floating_point{3.5};
  const Value string{"CurioDB"};

  EXPECT_EQ(integer.type(), DataTypeKind::Integer);
  EXPECT_EQ(integer.as_integer(), 42);
  EXPECT_EQ(floating_point.type(), DataTypeKind::Double);
  EXPECT_DOUBLE_EQ(floating_point.as_double(), 3.5);
  EXPECT_EQ(string.type(), DataTypeKind::Varchar);
  EXPECT_EQ(string.as_string(), "CurioDB");
}

TEST(ValueTest, RejectsAccessUsingTheWrongType) {
  const Value value{std::int64_t{42}};

  EXPECT_THROW(static_cast<void>(value.as_double()), std::bad_variant_access);
  EXPECT_THROW(static_cast<void>(value.as_string()), std::bad_variant_access);
}

TEST(ValueTest, FormatsValuesForDisplay) {
  EXPECT_EQ(Value{std::int64_t{-42}}.to_string(), "-42");
  EXPECT_EQ(Value{3.5}.to_string(), "3.5");
  EXPECT_EQ(Value{"Alice"}.to_string(), "Alice");
}

TEST(ValueTest, ComparesValuesOfTheSameType) {
  EXPECT_TRUE(Value{std::int64_t{1}} < Value{std::int64_t{2}});
  EXPECT_TRUE(Value{2.5} > Value{1.5});
  EXPECT_TRUE(Value{"Alice"} < Value{"Bob"});
  EXPECT_EQ(Value{std::int64_t{7}}, Value{std::int64_t{7}});
}

TEST(ValueTest, DoesNotOrderValuesOfDifferentTypes) {
  const auto ordering = Value{std::int64_t{1}} <=> Value{1.0};

  EXPECT_EQ(ordering, std::partial_ordering::unordered);
  EXPECT_NE(Value{std::int64_t{1}}, Value{1.0});
}

}  // namespace
}  // namespace curiodb

